package io.github.christmasstar.ui

import android.app.Application
import android.content.Context
import androidx.core.content.edit
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import io.github.christmasstar.ble.StarConnection
import io.github.christmasstar.effects.EFFECTS
import io.github.christmasstar.effects.EffectInfo
import io.github.christmasstar.protocol.EffectCatalog
import io.github.christmasstar.protocol.Firmware
import io.github.christmasstar.protocol.PreviewClip
import io.github.christmasstar.protocol.Pairing
import io.github.christmasstar.protocol.StarClient
import io.github.christmasstar.protocol.StarState
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import java.io.File
import java.io.InputStream

enum class Target { Front, Both, Back }

/** Firmware update progress, shown in a dialog. */
data class FwUpdateState(
    val running: Boolean = false,
    val stage: String = "",
    val progress: Float = 0f,
    val error: String? = null,
    val done: String? = null,
)

data class UploadState(
    val running: Boolean = false,
    val stage: String = "",
    val progress: Float = 0f,
    val error: String? = null,
    val done: Boolean = false,
)

class StarViewModel(app: Application) : AndroidViewModel(app) {
    val connection = StarConnection(app)
    private val client = StarClient(connection)
    private val prefs = app.getSharedPreferences("ui", Context.MODE_PRIVATE)

    private val _star = MutableStateFlow<StarState?>(null)
    val star: StateFlow<StarState?> = _star.asStateFlow()

    // Manual colours, remembered between sessions (0xRRGGBB).
    private val _front = MutableStateFlow(prefs.getInt("front", 0xFFB040))
    val front: StateFlow<Int> = _front.asStateFlow()
    private val _back = MutableStateFlow(prefs.getInt("back", 0xFFB040))
    val back: StateFlow<Int> = _back.asStateFlow()
    private val _target = MutableStateFlow(Target.entries[prefs.getInt("target", Target.Both.ordinal)])
    val target: StateFlow<Target> = _target.asStateFlow()

    private val _brightness = MutableStateFlow(128)
    val brightness: StateFlow<Int> = _brightness.asStateFlow()
    private val _shake = MutableStateFlow(true)
    val shake: StateFlow<Boolean> = _shake.asStateFlow()
    private val _speed = MutableStateFlow(100)
    val speed: StateFlow<Int> = _speed.asStateFlow()
    private val _effect = MutableStateFlow<String?>(null)
    val effect: StateFlow<String?> = _effect.asStateFlow()

    // ---- Pairing ----------------------------------------------------------------
    // One star per phone: this install's own key, the star it is paired with,
    // and an optional developer key (developer mode: any star, switch freely).
    private val pairPrefs = app.getSharedPreferences("pairing", Context.MODE_PRIVATE)
    // Kept apart from the pairing data: excluded from backups (res/xml/*_rules.xml).
    private val devPrefs = app.getSharedPreferences("dev", Context.MODE_PRIVATE)
    private val ownerKey: ByteArray = pairPrefs.getString("owner_key", null)?.let { Pairing.parseKey(it) }
        ?: Pairing.newKey().also { pairPrefs.edit { putString("owner_key", Pairing.hex(it)) } }
    private val _pairedAddress = MutableStateFlow(pairPrefs.getString("paired", null))
    val pairedAddress: StateFlow<String?> = _pairedAddress.asStateFlow()
    private val _devMode = MutableStateFlow(devPrefs.getString("dev_key", null) != null)
    val devMode: StateFlow<Boolean> = _devMode.asStateFlow()
    private val _access = MutableStateFlow<String?>(null)
    /** "owner", "dev" or "open" while unlocked, else null. */
    val access: StateFlow<String?> = _access.asStateFlow()
    private val _starName = MutableStateFlow("")
    val starName: StateFlow<String> = _starName.asStateFlow()
    private val _showStars = MutableStateFlow(false)
    /** The star chooser is open (asked for when no star is paired yet). */
    val showStars: StateFlow<Boolean> = _showStars.asStateFlow()
    private val _notice = MutableStateFlow<String?>(null)
    /** A hint for the star chooser, e.g. how to pair a star. */
    val notice: StateFlow<String?> = _notice.asStateFlow()
    private val _busy = MutableStateFlow<String?>(null)
    /** Address being connected from the chooser. */
    val busy: StateFlow<String?> = _busy.asStateFlow()

    // ---- Effects offered by the star ------------------------------------------
    // Firmware 1.4+ lists its effects and renders their preview clips, so new
    // effects need no app update. Older firmware: the app's own list.
    private val _effects = MutableStateFlow(EFFECTS)
    val effects: StateFlow<List<EffectInfo>> = _effects.asStateFlow()
    private val _clips = MutableStateFlow<Map<String, PreviewClip>>(emptyMap())
    val clips: StateFlow<Map<String, PreviewClip>> = _clips.asStateFlow()
    private var starFw = ""
    private var starProto = 1
    private var clipJob: Job? = null
    @Volatile private var lastUserAction = 0L

    private fun userAction() {
        lastUserAction = System.currentTimeMillis()
    }

    private suspend fun loadEffects() {
        _effects.value = if (starProto >= 3) {
            runCatching { EffectCatalog.parseList(client.command("app effects")) }.getOrNull()
                ?.takeIf { it.isNotEmpty() } ?: EFFECTS
        } else {
            EFFECTS
        }
    }

    /** Fetch the preview clips not yet cached (one at a time, behind user commands). */
    fun loadPreviews() {
        if (clipJob?.isActive == true || starProto < 3 || starFw.isEmpty()) return
        val fw = starFw
        val dir = File(getApplication<Application>().filesDir, "previews/$fw")
        clipJob = viewModelScope.launch {
            for (fx in _effects.value) {
                if (fx.id in _clips.value || fw != starFw) continue
                val file = File(dir, "${fx.id}.bin")
                var raw = if (file.isFile) runCatching { file.readBytes() }.getOrNull() else null
                if (raw == null) {
                    // Let taps and sliders go first.
                    while (System.currentTimeMillis() - lastUserAction < 1500) delay(300)
                    if (!ready) return@launch
                    raw = runCatching { client.raw("app preview ${fx.id}", "preview end") }.getOrNull()
                        ?: return@launch
                }
                val clip = PreviewClip.decode(raw) ?: continue
                if (!file.isFile) runCatching { dir.mkdirs(); file.writeBytes(raw) }
                _clips.value = _clips.value + (fx.id to clip)
            }
        }
    }

    // ---- Star firmware updates --------------------------------------------------
    // The app carries the newest signed star firmware (assets/firmware) and
    // offers it to stars running an older version that has the bootloader.
    private val bundledFirmware: ByteArray? = runCatching {
        app.assets.open(Firmware.ASSET).use { it.readBytes() }
    }.getOrNull()
    val bundledVersion: Firmware.Version? = bundledFirmware?.let { Firmware.imageVersion(it) }
    private val _starFirmware = MutableStateFlow("")
    /** Firmware version of the connected star ("" when unknown). */
    val starFirmware: StateFlow<String> = _starFirmware.asStateFlow()
    private val _fwUpdate = MutableStateFlow(FwUpdateState())
    val fwUpdate: StateFlow<FwUpdateState> = _fwUpdate.asStateFlow()

    /** The connected star can be updated to the bundled firmware. */
    fun firmwareUpdateAvailable(): Boolean =
        _access.value != null && Firmware.updateAvailable(_starFirmware.value, bundledVersion)

    fun updateFirmware() {
        val image = bundledFirmware ?: return
        val address = connection.address ?: return
        if (_fwUpdate.value.running || !firmwareUpdateAvailable()) return
        val target = bundledVersion.toString()
        viewModelScope.launch {
            _fwUpdate.value = FwUpdateState(running = true, stage = "Sending firmware $target to the star…")
            try {
                client.setLive(null)
                client.upload(image.inputStream(), image.size.toLong(), { sent ->
                    _fwUpdate.value = _fwUpdate.value.copy(progress = sent.toFloat() / image.size)
                }, { false }, command = "fw upload")
                val out = client.command("fw apply")
                if (!out.contains("ok restarting")) throw IllegalStateException(out.ifEmpty { "The star refused the update" })
                _fwUpdate.value = _fwUpdate.value.copy(stage = "The star is installing the update…", progress = 1f)
                client.reset()
                connection.disconnect()
                _access.value = null
                // The bootloader checks the signature and swaps the firmware (about 30 s).
                delay(15_000)
                val deadline = System.currentTimeMillis() + 120_000
                while (System.currentTimeMillis() < deadline && _access.value == null) {
                    connectTo(address, pair = false)
                    delay(12_000)
                }
                when {
                    _access.value == null ->
                        _fwUpdate.value = FwUpdateState(error = "The star did not come back. Unplug it and plug it back in.")
                    _starFirmware.value == target ->
                        _fwUpdate.value = FwUpdateState(done = "The star now runs firmware $target.")
                    else ->
                        _fwUpdate.value = FwUpdateState(
                            error = "The star went back to firmware ${_starFirmware.value}. It may need the update again later.",
                        )
                }
            } catch (e: Exception) {
                _fwUpdate.value = FwUpdateState(error = e.message ?: "The update failed")
            }
        }
    }

    fun dismissFwUpdate() {
        if (!_fwUpdate.value.running) _fwUpdate.value = FwUpdateState()
    }

    val liveFps = client.liveFps
    private val _upload = MutableStateFlow(UploadState())
    val upload: StateFlow<UploadState> = _upload.asStateFlow()

    /** Measured upload rate in bytes/s, for time estimates. */
    var uploadRate: Float = prefs.getFloat("rate", 5000f)
        private set

    private var clientJob: Job? = null
    private var pendingManual: String? = null
    private var manualJob: Job? = null
    private var brightnessJob: Job? = null
    @Volatile private var cancelUpload = false

    init {
        clientJob = client.start(viewModelScope)
        // Reconnect automatically when the link drops.
        viewModelScope.launch {
            connection.state.collect { s ->
                if (s == StarConnection.State.Failed && connection.error.value == "Connection lost") {
                    client.reset()
                    _access.value = null
                    delay(1500)
                    connection.address?.let { connectTo(it, pair = false) }
                }
            }
        }
    }

    /** Connect to this phone's star; without one, open the star chooser. */
    fun connect() {
        val address = _pairedAddress.value ?: devPrefs.getString("last_dev", null)?.takeIf { _devMode.value }
        if (address == null) {
            openStars()
            return
        }
        connectTo(address, pair = false)
    }

    fun openStars() {
        _showStars.value = true
        rescan()
    }

    fun closeStars() {
        _showStars.value = false
        _notice.value = null
    }

    fun rescan() {
        viewModelScope.launch {
            if (!connection.scanning.value) runCatching { connection.scan() }
        }
    }

    private fun keys(): List<ByteArray> {
        val dev = devPrefs.getString("dev_key", null)?.let { Pairing.parseKey(it) }
        return if (_devMode.value && dev != null) listOf(dev, ownerKey) else listOf(ownerKey)
    }

    /** Choose a star in the chooser: connect, and pair if its pairing window is open. */
    fun chooseStar(address: String) = connectTo(address, pair = true)

    private fun connectTo(address: String, pair: Boolean) {
        viewModelScope.launch {
            if (connection.state.value == StarConnection.State.Scanning ||
                connection.state.value == StarConnection.State.Connecting
            ) return@launch
            _busy.value = address
            client.reset()
            _access.value = null
            try {
                if (!connection.connect(address)) return@launch
                val r = runCatching { client.login(keys(), if (pair) ownerKey else null) }.getOrElse {
                    connection.disconnect()
                    connection.fail(it.message ?: "The star does not answer")
                    return@launch
                }
                if (!r.unlocked) {
                    connection.disconnect()
                    val who = r.hello.name.ifEmpty { "This star" }
                    _notice.value = if (r.hello.paired) {
                        "$who is paired with another phone. To pair it with this one, shake the star " +
                            "for 5 seconds until it glows gold, then tap it here again."
                    } else {
                        "$who is ready for a new owner, but its pairing time has run out. Unplug it " +
                            "and plug it back in (or shake it for 5 seconds), then tap it here within a minute."
                    }
                    connection.fail("Not paired with this phone")
                    _showStars.value = true
                    return@launch
                }
                _access.value = r.access
                _starName.value = r.hello.name
                _starFirmware.value = r.hello.fw
                if (r.hello.fw != starFw || r.hello.proto != starProto) {
                    starFw = r.hello.fw
                    starProto = r.hello.proto
                    _clips.value = emptyMap()
                }
                loadEffects()
                if (r.access == "owner") {
                    _pairedAddress.value = address
                    pairPrefs.edit { putString("paired", address) }
                } else if (r.access == "dev") {
                    devPrefs.edit { putString("last_dev", address) }
                }
                _notice.value = null
                _showStars.value = false
                refresh()
            } finally {
                _busy.value = null
            }
        }
    }

    /** Release the connected star from its owner; it can then pair with another phone. */
    fun unpair() {
        viewModelScope.launch {
            val force = if (_access.value == "dev") " force" else ""
            val out = runCatching { client.command("star unpair$force") }.getOrDefault("")
            if (out.contains("ok released")) {
                if (connection.address == _pairedAddress.value) {
                    _pairedAddress.value = null
                    pairPrefs.edit { remove("paired") }
                }
                client.reset()
                connection.disconnect()
                _access.value = null
                _notice.value = "The star is released. Any phone can pair with it during the first " +
                    "minute after it is plugged in."
                openStars()
            } else {
                connection.fail(out.ifEmpty { "Could not release the star" })
            }
        }
    }

    /** Rename the star (1-16 plain characters). Its Bluetooth restarts; we reconnect. */
    fun rename(name: String) {
        if (!Pairing.validName(name)) return
        viewModelScope.launch {
            val out = runCatching { client.command("star name $name") }.getOrDefault("")
            if (!out.contains("ok renaming")) {
                connection.fail(out.ifEmpty { "Rename failed" })
                return@launch
            }
            _starName.value = name
            val address = connection.address ?: return@launch
            client.reset()
            connection.disconnect()
            delay(6000)
            connectTo(address, pair = false)
        }
    }

    /** Developer mode: control any star built with this developer key (32 hex digits). */
    fun enableDevMode(keyText: String): Boolean {
        val key = Pairing.parseKey(keyText) ?: return false
        devPrefs.edit { putString("dev_key", Pairing.hex(key)) }
        _devMode.value = true
        return true
    }

    fun disableDevMode() {
        devPrefs.edit { remove("dev_key"); remove("last_dev") }
        _devMode.value = false
        if (_access.value == "dev") {
            client.reset()
            connection.disconnect()
            _access.value = null
        }
    }

    fun refresh() {
        viewModelScope.launch {
            val s = runCatching { StarState.parse(client.command("app state")) }.getOrNull() ?: return@launch
            _star.value = s
            _brightness.value = s.brightness
            _shake.value = s.shake
            _speed.value = s.speed
            _effect.value = s.effect
            if (s.name.isNotEmpty()) _starName.value = s.name
        }
    }

    private val ready get() = connection.state.value == StarConnection.State.Ready

    // ---- Manual -------------------------------------------------------------

    fun setChannel(channel: Int, value: Int) {
        val shift = 16 - 8 * channel
        fun put(c: Int) = (c and (0xFF shl shift).inv()) or (value shl shift)
        when (_target.value) {
            Target.Front -> _front.value = put(_front.value)
            Target.Back -> _back.value = put(_back.value)
            Target.Both -> {
                _front.value = put(_front.value)
                _back.value = _front.value
            }
        }
        sendManual()
    }

    fun setColor(rgb: Int) {
        when (_target.value) {
            Target.Front -> _front.value = rgb
            Target.Back -> _back.value = rgb
            Target.Both -> { _front.value = rgb; _back.value = rgb }
        }
        sendManual()
    }

    fun setTarget(t: Target) {
        _target.value = t
        if (t == Target.Both) {
            // The front is the master: the back takes its colour.
            _back.value = _front.value
            sendManual()
        }
        prefs.edit { putInt("target", t.ordinal) }
    }

    /** The colour the sliders currently edit. */
    fun editedColor(): Int = if (_target.value == Target.Back) _back.value else _front.value

    private fun sendManual() {
        userAction()
        prefs.edit { putInt("front", _front.value); putInt("back", _back.value) }
        _effect.value = "manual"
        pendingManual = when (_target.value) {
            Target.Both -> "set_all 0 0x%06X".format(_front.value)
            Target.Front -> "set_all 1 0x%06X".format(_front.value)
            Target.Back -> "set_all 2 0x%06X".format(_back.value)
        }
        // Latest value wins, at most ~25 updates per second.
        if (manualJob?.isActive == true) return
        manualJob = viewModelScope.launch {
            while (true) {
                val cmd = pendingManual ?: break
                pendingManual = null
                if (ready) client.send(cmd)
                delay(40)
            }
        }
    }

    /** Apply the manual colours to the star (e.g. when opening the page). */
    fun applyManual() {
        if (!ready) return
        if (_target.value == Target.Both || _front.value == _back.value) {
            client.send("set_all 0 0x%06X".format(_front.value))
        } else {
            client.send("set_all 1 0x%06X".format(_front.value))
            client.send("set_all 2 0x%06X".format(_back.value))
        }
        _effect.value = "manual"
    }

    // ---- Global controls -----------------------------------------------------

    fun setBrightness(v: Int) {
        userAction()
        _brightness.value = v
        // The star stores brightness in flash; don't hammer it while dragging.
        if (brightnessJob?.isActive == true) return
        brightnessJob = viewModelScope.launch {
            var sent = -1
            while (sent != _brightness.value) {
                sent = _brightness.value
                if (ready) client.send("led brightness $sent")
                delay(250)
            }
        }
    }

    fun setShake(on: Boolean) {
        userAction()
        _shake.value = on
        if (ready) client.send(if (on) "shake on" else "shake off")
    }

    fun setSpeed(v: Int) {
        userAction()
        _speed.value = v
        if (ready) client.send("effect speed $v")
    }

    fun playEffect(id: String) {
        userAction()
        _effect.value = id
        if (ready) client.send("effect play $id")
    }

    // ---- Live picture / video ---------------------------------------------------

    /** Stream 66 front colours (back copies front); null stops. */
    fun setLive(front: IntArray?) {
        if (front != null) _effect.value = "manual"
        if (ready || front == null) client.setLive(front)
    }

    /** Upload a .csfx effect and play it. */
    fun uploadEffect(prepare: suspend (progress: (Float) -> Unit) -> Pair<InputStream, Long>) {
        if (_upload.value.running) return
        cancelUpload = false
        viewModelScope.launch {
            _upload.value = UploadState(running = true, stage = "Preparing frames…")
            try {
                val (stream, size) = prepare { p ->
                    _upload.value = _upload.value.copy(progress = p * 0.25f)
                    if (cancelUpload) throw CancelledException()
                }
                _upload.value = _upload.value.copy(stage = "Sending to the star…", progress = 0.25f)
                val t0 = System.currentTimeMillis()
                stream.use {
                    client.upload(it, size, { sent ->
                        _upload.value = _upload.value.copy(progress = 0.25f + 0.75f * sent / size)
                    }, { cancelUpload })
                }
                val secs = (System.currentTimeMillis() - t0) / 1000f
                if (secs > 2f) {
                    uploadRate = size / secs
                    prefs.edit { putFloat("rate", uploadRate) }
                }
                client.command("effect play flash")
                _upload.value = UploadState(done = true, stage = "Saved on the star")
                refresh()
            } catch (e: CancelledException) {
                _upload.value = UploadState(error = "Cancelled")
            } catch (e: Exception) {
                _upload.value = UploadState(error = e.message ?: "Upload failed")
            }
        }
    }

    fun cancelUpload() {
        cancelUpload = true
    }

    fun dismissUpload() {
        if (!_upload.value.running) _upload.value = UploadState()
    }

    override fun onCleared() {
        client.setLive(null)
        connection.disconnect()
    }

    class CancelledException : Exception("cancelled")
}
