package io.github.christmasstar.ble

import android.annotation.SuppressLint
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothGattDescriptor
import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothProfile
import android.bluetooth.BluetoothStatusCodes
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.Context
import android.os.Build
import io.github.christmasstar.protocol.ByteQueue
import io.github.christmasstar.protocol.StarLink
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.suspendCancellableCoroutine
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withTimeout
import kotlinx.coroutines.withTimeoutOrNull
import java.util.UUID
import kotlin.coroutines.resume

/**
 * BLE link to the star's BM78 module ("Dual-SPP") using the Microchip
 * Transparent UART service.
 *
 * Callers must hold the Bluetooth permissions (checked by the UI).
 */
@SuppressLint("MissingPermission")
class StarConnection(private val context: Context) : StarLink {

    enum class State { Idle, Scanning, Connecting, Ready, Failed }

    private val _state = MutableStateFlow(State.Idle)
    val state: StateFlow<State> = _state.asStateFlow()
    private val _error = MutableStateFlow<String?>(null)
    val error: StateFlow<String?> = _error.asStateFlow()

    override val input = ByteQueue()

    private val adapter get() = context.getSystemService(BluetoothManager::class.java)?.adapter

    private var gatt: BluetoothGatt? = null
    private var rxChar: BluetoothGattCharacteristic? = null
    private var chunk = 20
    private val writeLock = Mutex()
    private var writeDone: CompletableDeferred<Boolean>? = null
    private val events = Channel<String>(Channel.UNLIMITED)

    val bluetoothEnabled: Boolean get() = adapter?.isEnabled == true

    /** A star seen while scanning. */
    data class FoundStar(val address: String, val name: String, val rssi: Int)

    private val _nearby = MutableStateFlow<List<FoundStar>>(emptyList())
    /** Stars seen by the last scans, strongest first. */
    val nearby: StateFlow<List<FoundStar>> = _nearby.asStateFlow()
    private val _scanning = MutableStateFlow(false)
    val scanning: StateFlow<Boolean> = _scanning.asStateFlow()

    /** Address of the star the link is (or was last) connected to. */
    var address: String? = null
        private set

    /** Connect to the star with this address. Returns true when ready. */
    suspend fun connect(address: String): Boolean {
        disconnect()
        _error.value = null
        try {
            _state.value = State.Scanning
            val device = findStar(address) ?: run {
                fail("The star was not found. Is it powered and in range?")
                return false
            }
            _state.value = State.Connecting
            withTimeout(20_000) { open(device) }
            this.address = address
            _state.value = State.Ready
            return true
        } catch (e: Exception) {
            disconnect()
            fail(e.message ?: "Connection failed")
            return false
        }
    }

    fun disconnect() {
        gatt?.let {
            it.disconnect()
            it.close()
        }
        gatt = null
        rxChar = null
        writeDone?.complete(false)
        if (_state.value != State.Failed) _state.value = State.Idle
    }

    /** Show a problem in the connection chip without a link change. */
    fun fail(msg: String) {
        _error.value = msg
        _state.value = State.Failed
    }

    private fun isStar(r: ScanResult): Boolean {
        val name = r.scanRecord?.deviceName ?: r.device.name
        val hasService = r.scanRecord?.serviceUuids?.any { it.uuid == SERVICE } == true
        return hasService || name == STAR_NAME
    }

    private fun remember(r: ScanResult) {
        val name = r.scanRecord?.deviceName ?: r.device.name ?: STAR_NAME
        val found = FoundStar(r.device.address, name, r.rssi)
        _nearby.value = (_nearby.value.filter { it.address != found.address } + found).sortedByDescending { it.rssi }
    }

    /** Scan for stars for [durationMs]; results go to [nearby]. */
    suspend fun scan(durationMs: Long = 6000) {
        val scanner = adapter?.bluetoothLeScanner ?: return
        val cb = object : ScanCallback() {
            override fun onScanResult(callbackType: Int, r: ScanResult) {
                if (isStar(r)) remember(r)
            }
        }
        val settings = ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).build()
        _scanning.value = true
        scanner.startScan(null, settings, cb)
        try {
            kotlinx.coroutines.delay(durationMs)
        } finally {
            scanner.stopScan(cb)
            _scanning.value = false
        }
    }

    private suspend fun findStar(address: String): BluetoothDevice? {
        val adapter = adapter ?: return null
        val scanner = adapter.bluetoothLeScanner ?: return null
        val result = CompletableDeferred<BluetoothDevice>()
        val cb = object : ScanCallback() {
            override fun onScanResult(callbackType: Int, r: ScanResult) {
                if (isStar(r)) remember(r)
                if (r.device.address == address) result.complete(r.device)
            }

            override fun onScanFailed(errorCode: Int) {
                result.completeExceptionally(IllegalStateException("Bluetooth scan failed ($errorCode)"))
            }
        }
        val settings = ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).build()
        scanner.startScan(null, settings, cb)
        return try {
            withTimeoutOrNull(15_000) { result.await() }
        } finally {
            scanner.stopScan(cb)
        }
    }

    private suspend fun open(device: BluetoothDevice) {
        while (events.tryReceive().isSuccess) {
            // drop stale events from an earlier connection
        }
        @Suppress("DEPRECATION")
        val g = device.connectGatt(context, false, callback, BluetoothDevice.TRANSPORT_LE)
            ?: throw IllegalStateException("Could not open a connection")
        gatt = g
        expect("connected")
        g.requestConnectionPriority(BluetoothGatt.CONNECTION_PRIORITY_HIGH)
        g.requestMtu(247)
        expect("mtu")
        g.discoverServices()
        expect("services")
        val svc = g.getService(SERVICE) ?: throw IllegalStateException("The star's UART service is missing")
        val tx = svc.getCharacteristic(TX_CHAR) ?: throw IllegalStateException("TX characteristic missing")
        rxChar = svc.getCharacteristic(RX_CHAR) ?: throw IllegalStateException("RX characteristic missing")
        g.setCharacteristicNotification(tx, true)
        val cccd = tx.getDescriptor(CCCD)
        if (Build.VERSION.SDK_INT >= 33) {
            g.writeDescriptor(cccd, BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE)
        } else {
            @Suppress("DEPRECATION")
            cccd.value = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
            @Suppress("DEPRECATION")
            g.writeDescriptor(cccd)
        }
        expect("descriptor")
    }

    private suspend fun expect(event: String) {
        while (true) {
            val e = events.receive()
            if (e == event) return
            if (e == "disconnected") throw IllegalStateException("The star disconnected")
        }
    }

    override suspend fun write(data: ByteArray) {
        writeLock.withLock {
            var at = 0
            while (at < data.size) {
                val g = gatt ?: throw IllegalStateException("Not connected")
                val c = rxChar ?: throw IllegalStateException("Not connected")
                val part = data.copyOfRange(at, minOf(data.size, at + chunk))
                val done = CompletableDeferred<Boolean>()
                writeDone = done
                val type = BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE
                val started = if (Build.VERSION.SDK_INT >= 33) {
                    g.writeCharacteristic(c, part, type) == BluetoothStatusCodes.SUCCESS
                } else {
                    @Suppress("DEPRECATION")
                    c.writeType = type
                    @Suppress("DEPRECATION")
                    c.value = part
                    @Suppress("DEPRECATION")
                    g.writeCharacteristic(c)
                }
                if (!started) {
                    // The stack's queue is full; give it a moment.
                    kotlinx.coroutines.delay(5)
                    continue
                }
                if (withTimeoutOrNull(2000) { done.await() } != true) {
                    throw IllegalStateException("Write failed")
                }
                at += part.size
            }
        }
    }

    private val callback = object : BluetoothGattCallback() {
        override fun onConnectionStateChange(g: BluetoothGatt, status: Int, newState: Int) {
            if (newState == BluetoothProfile.STATE_CONNECTED && status == BluetoothGatt.GATT_SUCCESS) {
                events.trySend("connected")
            } else if (newState == BluetoothProfile.STATE_DISCONNECTED) {
                events.trySend("disconnected")
                writeDone?.complete(false)
                if (_state.value == State.Ready) {
                    _error.value = "Connection lost"
                    _state.value = State.Failed
                }
            }
        }

        override fun onMtuChanged(g: BluetoothGatt, mtu: Int, status: Int) {
            // 128-byte writes were measured to work well with the BM78.
            chunk = if (status == BluetoothGatt.GATT_SUCCESS) (mtu - 3).coerceIn(20, 128) else 20
            events.trySend("mtu")
        }

        override fun onServicesDiscovered(g: BluetoothGatt, status: Int) {
            events.trySend("services")
        }

        override fun onDescriptorWrite(g: BluetoothGatt, d: BluetoothGattDescriptor, status: Int) {
            events.trySend("descriptor")
        }

        override fun onCharacteristicWrite(g: BluetoothGatt, c: BluetoothGattCharacteristic, status: Int) {
            writeDone?.complete(status == BluetoothGatt.GATT_SUCCESS)
        }

        override fun onCharacteristicChanged(g: BluetoothGatt, c: BluetoothGattCharacteristic, value: ByteArray) {
            input.push(value)
        }

        @Deprecated("Used before Android 13")
        override fun onCharacteristicChanged(g: BluetoothGatt, c: BluetoothGattCharacteristic) {
            if (Build.VERSION.SDK_INT < 33) {
                @Suppress("DEPRECATION")
                c.value?.let { input.push(it) }
            }
        }
    }

    companion object {
        const val STAR_NAME = "Dual-SPP"
        val SERVICE: UUID = UUID.fromString("49535343-fe7d-4ae5-8fa9-9fafd205e455")
        val TX_CHAR: UUID = UUID.fromString("49535343-1e4d-4bd9-ba61-23c647249616")
        val RX_CHAR: UUID = UUID.fromString("49535343-8841-43f4-a8d4-ecbe34729bb3")
        val CCCD: UUID = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")
    }
}
