package io.github.christmasstar.protocol

import io.github.christmasstar.effects.EffectInfo

/**
 * The effects a star offers (`app effects`, firmware protocol 3) and the
 * preview clips it renders for them (`app preview`), so new effects reach the
 * app with a firmware update alone.
 */
object EffectCatalog {
    /** Parse "fx <id> <flags> <title>|<subtitle>" lines. */
    fun parseList(text: String): List<EffectInfo> = text.replace("\r", "").lines().mapNotNull { line ->
        val parts = line.trim().split(' ', limit = 4)
        if (parts.size < 4 || parts[0] != "fx") return@mapNotNull null
        val title = parts[3].substringBefore('|')
        val subtitle = parts[3].substringAfter('|', "")
        EffectInfo(parts[1], title, subtitle, tilt = 't' in parts[2], twoFaces = '2' in parts[2])
    }
}

/** A preview clip: [frames] of 66 (or 132 for two-faced effects) colours 0xRRGGBB. */
class PreviewClip(val stepMs: Int, val frames: List<IntArray>) {
    fun frameAt(timeMs: Long): IntArray = frames[((timeMs / stepMs) % frames.size).toInt()]

    companion object {
        /**
         * Decode the reply of `app preview`: a text line with "step=", then
         * stream frames (A5 5A | flags | n | n x RGB565 LE | CRC-16 BE).
         */
        fun decode(raw: ByteArray): PreviewClip? {
            val text = String(raw, Charsets.ISO_8859_1)
            val step = Regex("step=(\\d+)").find(text)?.groupValues?.get(1)?.toIntOrNull() ?: 100
            val frames = mutableListOf<IntArray>()
            var i = 0
            while (i + 4 <= raw.size) {
                if (raw[i] != 0xA5.toByte() || raw[i + 1] != 0x5A.toByte()) {
                    i++
                    continue
                }
                val n = raw[i + 3].toInt() and 0xFF
                val end = i + 4 + 2 * n + 2
                if (end > raw.size) break
                val crc = ((raw[end - 2].toInt() and 0xFF) shl 8) or (raw[end - 1].toInt() and 0xFF)
                if (crc == crc16Xmodem(raw, i + 2, 2 + 2 * n)) {
                    frames += IntArray(n) { k ->
                        val v = (raw[i + 4 + 2 * k].toInt() and 0xFF) or ((raw[i + 5 + 2 * k].toInt() and 0xFF) shl 8)
                        rgb888(v)
                    }
                    i = end
                } else {
                    i += 2
                }
            }
            return if (frames.isEmpty()) null else PreviewClip(step, frames)
        }

        fun rgb888(v: Int): Int {
            val r = (v shr 11) and 0x1F
            val g = (v shr 5) and 0x3F
            val b = v and 0x1F
            return ((r shl 3 or (r shr 2)) shl 16) or ((g shl 2 or (g shr 4)) shl 8) or (b shl 3 or (b shr 2))
        }
    }
}
