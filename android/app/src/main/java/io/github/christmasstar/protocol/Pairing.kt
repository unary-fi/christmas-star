package io.github.christmasstar.protocol

import java.security.SecureRandom
import javax.crypto.Mac
import javax.crypto.spec.SecretKeySpec

/**
 * Star pairing (firmware protocol 2, see sw/src/pair/pairing.h).
 *
 * The star's Bluetooth shell is locked until the app proves a key:
 *   app hello          -> ok proto=2 ... owner=free|paired pairing=open|closed access=none nonce=<hex> name=<name>
 *   app auth <hmac>    -> ok access=owner|dev   (HMAC-SHA256(key, "auth" + nonce))
 *   app pair <key>     -> ok paired             (only while the star's pairing window is open)
 */
data class Hello(
    val proto: Int,
    val owner: String,
    val pairingOpen: Boolean,
    val access: String,
    val nonce: ByteArray,
    val name: String,
    val fw: String = "",
) {
    val paired get() = owner == "paired"

    companion object {
        fun parse(line: String): Hello? {
            val l = line.trim()
            if (!l.startsWith("ok ")) return null
            val nameAt = l.indexOf(" name=")
            val head = if (nameAt >= 0) l.substring(3, nameAt) else l.substring(3)
            val kv = head.split(' ').mapNotNull { p -> p.split('=', limit = 2).takeIf { it.size == 2 } }
                .associate { it[0] to it[1] }
            return Hello(
                proto = kv["proto"]?.toIntOrNull() ?: 1,
                owner = kv["owner"] ?: "free",
                pairingOpen = kv["pairing"] == "open",
                access = kv["access"] ?: "open",
                nonce = kv["nonce"]?.let { Pairing.unhex(it) } ?: ByteArray(0),
                name = if (nameAt >= 0) l.substring(nameAt + 6).trim() else "",
                fw = kv["fw"] ?: "",
            )
        }
    }

    override fun equals(other: Any?) = other is Hello && other.proto == proto && other.owner == owner &&
        other.pairingOpen == pairingOpen && other.access == access && other.nonce.contentEquals(nonce) &&
        other.name == name

    override fun hashCode() = nonce.contentHashCode() * 31 + name.hashCode()
}

/** Result of logging in: [access] is "owner", "dev", "open" (old firmware) or "none". */
data class LoginResult(val hello: Hello, val access: String) {
    val unlocked get() = access != "none"
}

object Pairing {
    const val KEY_LEN = 16
    const val NAME_MAX = 16

    fun newKey(): ByteArray = ByteArray(KEY_LEN).also { SecureRandom().nextBytes(it) }

    fun authMac(key: ByteArray, nonce: ByteArray): String {
        val mac = Mac.getInstance("HmacSHA256")
        mac.init(SecretKeySpec(key, "HmacSHA256"))
        mac.update("auth".toByteArray(Charsets.US_ASCII))
        return hex(mac.doFinal(nonce))
    }

    fun hex(b: ByteArray) = b.joinToString("") { "%02x".format(it.toInt() and 0xFF) }

    fun unhex(s: String): ByteArray? {
        if (s.length % 2 != 0 || !s.all { it.isDigit() || it.lowercaseChar() in 'a'..'f' }) return null
        return ByteArray(s.length / 2) { s.substring(2 * it, 2 * it + 2).toInt(16).toByte() }
    }

    /** A 16-byte key from 32 hex digits, or null. */
    fun parseKey(text: String): ByteArray? =
        unhex(text.trim().replace(" ", ""))?.takeIf { it.size == KEY_LEN && it.any { b -> b.toInt() != 0 } }

    fun validName(name: String) = name.length in 1..NAME_MAX && name.all { it.code in 32..126 }
}
