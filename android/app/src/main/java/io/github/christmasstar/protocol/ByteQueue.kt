package io.github.christmasstar.protocol

import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.withTimeoutOrNull

/**
 * Bytes received from the star (BLE notifications), read by one consumer.
 */
class ByteQueue {
    private val lock = Any()
    private val buf = ArrayDeque<Byte>()
    private val signal = Channel<Unit>(Channel.CONFLATED)

    fun push(data: ByteArray) {
        synchronized(lock) { data.forEach { buf.addLast(it) } }
        signal.trySend(Unit)
    }

    fun clear() {
        synchronized(lock) { buf.clear() }
    }

    private fun poll(): Int = synchronized(lock) {
        if (buf.isEmpty()) -1 else buf.removeFirst().toInt() and 0xFF
    }

    /** Next byte, or -1 after [timeoutMs]. */
    suspend fun read(timeoutMs: Long): Int {
        val deadline = System.currentTimeMillis() + timeoutMs
        while (true) {
            val b = poll()
            if (b >= 0) return b
            val left = deadline - System.currentTimeMillis()
            if (left <= 0) return -1
            withTimeoutOrNull(left) { signal.receive() }
        }
    }

    /** Read text until [token] appears or the timeout passes. */
    suspend fun readUntil(token: String, timeoutMs: Long): String {
        val deadline = System.currentTimeMillis() + timeoutMs
        val sb = StringBuilder()
        while (!sb.endsWith(token)) {
            val left = deadline - System.currentTimeMillis()
            if (left <= 0) break
            val b = read(left)
            if (b < 0) break
            sb.append(b.toChar())
        }
        return sb.toString()
    }
}
