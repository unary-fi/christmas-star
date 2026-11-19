package io.github.christmasstar.protocol

import java.io.InputStream

/** Byte link used by the XMODEM sender (the star's BLE link in the app). */
interface BytePort {
    suspend fun write(data: ByteArray)
    /** Next received byte, or -1 on timeout. */
    suspend fun read(timeoutMs: Long): Int
}

class XmodemException(message: String) : Exception(message)

/**
 * XMODEM-1K / CRC sender, matching the receiver in sw/src/transfer/xmodem.c.
 */
class XmodemSender(private val port: BytePort) {

    /**
     * Send [size] bytes from [source].
     * @param progress called with the number of bytes acknowledged so far
     * @param cancelled polled between blocks; sends CAN CAN when it returns true
     */
    suspend fun send(
        source: InputStream,
        size: Long,
        progress: (Long) -> Unit = {},
        cancelled: () -> Boolean = { false },
    ) {
        waitForStart()
        val block = ByteArray(BLOCK)
        var blockNo = 1
        var sent = 0L
        while (sent < size) {
            if (cancelled()) {
                port.write(byteArrayOf(CAN, CAN))
                throw XmodemException("cancelled")
            }
            val n = readFully(source, block)
            for (i in n until BLOCK) block[i] = PAD
            sendBlock(blockNo, block)
            sent += n
            blockNo = (blockNo + 1) and 0xFF
            progress(minOf(sent, size))
        }
        repeat(RETRIES) {
            port.write(byteArrayOf(EOT))
            if (port.read(RESPONSE_TIMEOUT_MS) == ACK.toInt()) return
        }
        throw XmodemException("no ACK for end of transfer")
    }

    private suspend fun waitForStart() {
        val deadline = System.currentTimeMillis() + START_TIMEOUT_MS
        while (System.currentTimeMillis() < deadline) {
            val b = port.read(deadline - System.currentTimeMillis())
            if (b == 'C'.code) return
            if (b == CAN.toInt()) throw XmodemException("receiver cancelled")
        }
        throw XmodemException("the star did not start the transfer")
    }

    private suspend fun sendBlock(no: Int, data: ByteArray) {
        val pkt = ByteArray(3 + BLOCK + 2)
        pkt[0] = STX
        pkt[1] = no.toByte()
        pkt[2] = (no.inv() and 0xFF).toByte()
        data.copyInto(pkt, 3)
        val crc = crc16Xmodem(data)
        pkt[3 + BLOCK] = (crc shr 8).toByte()
        pkt[4 + BLOCK] = (crc and 0xFF).toByte()

        repeat(RETRIES) {
            port.write(pkt)
            when (port.read(RESPONSE_TIMEOUT_MS)) {
                ACK.toInt() -> return
                CAN.toInt() -> throw XmodemException("the star cancelled the transfer")
                else -> Unit // NAK, 'C' or timeout: resend
            }
        }
        throw XmodemException("block $no was not accepted")
    }

    private fun readFully(input: InputStream, buf: ByteArray): Int {
        var n = 0
        while (n < buf.size) {
            val r = input.read(buf, n, buf.size - n)
            if (r < 0) break
            n += r
        }
        return n
    }

    companion object {
        const val BLOCK = 1024
        const val STX: Byte = 0x02
        const val EOT: Byte = 0x04
        const val ACK: Byte = 0x06
        const val CAN: Byte = 0x18
        const val PAD: Byte = 0x1A
        const val RETRIES = 10
        const val START_TIMEOUT_MS = 15_000L
        const val RESPONSE_TIMEOUT_MS = 6_000L
    }
}
