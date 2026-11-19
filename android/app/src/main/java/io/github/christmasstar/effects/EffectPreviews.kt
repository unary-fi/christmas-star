package io.github.christmasstar.effects

import io.github.christmasstar.star.StarLayout
import kotlin.math.abs
import kotlin.math.sqrt

/** An effect that runs on the star itself. */
data class EffectInfo(
    val id: String,
    val title: String,
    val subtitle: String,
    val tilt: Boolean = false,
    val twoFaces: Boolean = false,
)

/** Effects offered on the Effects page, in display order. */
val EFFECTS = listOf(
    EffectInfo("golden", "Golden Star", "Warm gold, gently shimmering"),
    EffectInfo("snowfall", "Snowfall", "Flakes drifting down through the night"),
    EffectInfo("candle", "Candlelight", "A soft flame in the heart of the star"),
    EffectInfo("aurora", "Northern Lights", "Green and violet curtains"),
    EffectInfo("candycane", "Candy Cane", "Red and white swirls"),
    EffectInfo("treelights", "Tree Lights", "Old-fashioned coloured bulbs"),
    EffectInfo("bethlehem", "Star of Bethlehem", "Radiant light from the centre"),
    EffectInfo("fireworks", "Fireworks", "Bursts for the New Year"),
    EffectInfo("frost", "Frost", "Ice crystals creeping in"),
    EffectInfo("twinkle", "Twinkle", "Warm glow with sparkles"),
    EffectInfo("comet", "Comets", "Chasing around the outline"),
    EffectInfo("rainbow", "Rainbow", "All the colours, turning"),
)

/**
 * Kotlin ports of the star's effects (sw/src/effects/fx_xmas.c, fx_basic.c,
 * fx_golden.c) for the animated previews. They use the same integer maths and
 * layout tables so the previews match the star. Only the front face (66 LEDs)
 * is rendered; the star upright (gravity straight down).
 */
class EffectPreview(private val id: String) {
    private val frame = IntArray(N)
    private var rng = 0x12345678
    private val heat = IntArray(N)
    private val flakes = Array(FLAKES) { IntArray(4) } // x16, y16, speed, wobble
    private var snowLast = 0L
    private val bursts = Array(3) { LongArray(5) } // x, y, hue, start, life
    private var started = false

    /** Render the frame at effect time [t] (ms). Returns 66 colours 0xRRGGBB. */
    fun render(t: Long): IntArray {
        if (!started) {
            start()
            started = true
        }
        when (id) {
            "golden" -> golden(t)
            "snowfall" -> snowfall(t)
            "candle" -> candle()
            "aurora" -> aurora(t)
            "candycane" -> candy(t)
            "treelights" -> tree(t)
            "bethlehem" -> bethlehem(t)
            "fireworks" -> fireworks(t)
            "frost" -> frost(t)
            "twinkle" -> twinkle()
            "comet" -> comet(t)
            "rainbow" -> rainbow(t)
            "flash" -> flashPlaceholder(t)
        }
        return frame
    }

    private fun start() {
        when (id) {
            "snowfall" -> for (f in flakes) spawnFlake(f, true)
            "twinkle" -> frame.fill(rgb(60, 35, 6))
        }
    }

    // ---- helpers (match the firmware) ---------------------------------------

    private fun rand(): Int {
        var x = rng
        x = x xor (x shl 13)
        x = x xor (x ushr 17)
        x = x xor (x shl 5)
        rng = x
        return x ushr 1 // non-negative
    }

    private fun golden(t: Long) {
        for (i in 0 until N) {
            val shimmer = tri((t / 10 + A[i] + R[i]).toInt())
            frame[i] = scale(rgb(255, 165, 35), 170 + shimmer * 85 / 255)
        }
    }

    private fun spawnFlake(f: IntArray, anywhere: Boolean) {
        // Gravity straight down: gx = 0, gy = -256.
        val side = rand() % 240 - 120
        val back = if (anywhere) rand() % 240 - 120 else -125
        f[0] = (256 * side) * 16 / 256
        f[1] = (-256 * back) * 16 / 256
        f[2] = 6 + rand() % 10
        f[3] = rand() and 0xFF
    }

    private fun snowfall(t: Long) {
        val dt = minOf(t - snowLast, 100L).toInt().coerceAtLeast(0)
        snowLast = t
        frame.fill(rgb(2, 6, 28))
        for (f in flakes) {
            val step = f[2] * dt / 8
            val drift = (tri(f[3] + (t / 12).toInt()) - 128) * dt / 256
            f[0] += (256 * drift) / 256
            f[1] += (-256 * step) / 256
            val x = f[0] / 16
            val y = f[1] / 16
            if (abs(x) > 130 || abs(y) > 130) {
                spawnFlake(f, false)
                continue
            }
            splat(x, y, rgb(210, 225, 255), 14)
        }
    }

    private fun splat(x: Int, y: Int, c: Int, radius: Int) {
        val r2 = radius * radius
        for (i in 0 until N) {
            val dx = X[i] - x
            val dy = Y[i] - y
            val d2 = dx * dx + dy * dy
            if (d2 < r2) frame[i] = add(frame[i], scale(c, 255 - d2 * 255 / r2))
        }
    }

    private fun candle() {
        for (i in 0 until N) {
            val target = 150 + rand() % 106
            heat[i] = heat[i] + (target - heat[i]) / 5
            val glow = if (G[i] == 1) heat[i] else heat[i] * (140 - R[i]) / 90
            val c = mix(rgb(255, 70, 0), rgb(255, 170, 50), heat[i])
            frame[i] = scale(c, glow.coerceIn(20, 255))
        }
    }

    private fun aurora(t: Long) {
        for (i in 0 until N) {
            val w1 = tri((X[i] * 2 + t / 24).toInt())
            val w2 = tri((Y[i] * 3 - X[i] + t / 37).toInt())
            val lum = w1 * w2 / 255
            val hue = (80 + tri((Y[i] + t / 50).toInt()) * 110 / 255) and 0xFF
            frame[i] = hsv(hue, 210, 15 + lum * 240 / 255)
        }
    }

    private fun candy(t: Long) {
        for (i in 0 until N) {
            val phase = ((A[i] + R[i] * 2 - t / 10) * 4).toInt() and 0xFF
            val s = ((tri(phase) - 96) * 4).coerceIn(0, 255)
            frame[i] = mix(rgb(230, 10, 20), rgb(255, 235, 220), s)
        }
    }

    private fun tree(t: Long) {
        val bulbs = intArrayOf(rgb(230, 20, 10), rgb(20, 200, 40), rgb(255, 160, 20), rgb(30, 60, 255))
        for (i in 0 until N) {
            frame[i] = if (G[i] == 0) {
                val colour = S[i] % 4
                val b = tri((t / (7 + colour) + colour * 64).toInt())
                scale(bulbs[colour], 50 + b * 205 / 255)
            } else {
                val b = tri((t / 20 + A[i]).toInt())
                scale(rgb(255, 200, 120), 60 + b / 3)
            }
        }
    }

    private fun bethlehem(t: Long) {
        for (i in 0 until N) {
            val wave = tri((R[i] * 3 - t / 5).toInt())
            val tip = G[i] == 0 && S[i] % 8 == 4
            val core = if (R[i] < 40) 120 else 0
            val lum = 40 + wave * 160 / 255 + core + (if (tip) 60 else 0)
            val c = mix(rgb(255, 150, 30), rgb(255, 245, 220), (255 - R[i] * 2).coerceIn(0, 255))
            frame[i] = scale(c, lum.coerceIn(0, 255))
        }
    }

    private fun fireworks(t: Long) {
        fade(200)
        for (bu in bursts) {
            val age = t - bu[3]
            if (bu[4] == 0L || age > bu[4]) {
                if (rand() % 40 == 0) {
                    bu[0] = (rand() % 120 - 60).toLong()
                    bu[1] = (rand() % 120 - 60).toLong()
                    bu[2] = (rand() and 0xFF).toLong()
                    bu[3] = t
                    bu[4] = (900 + rand() % 600).toLong()
                }
                continue
            }
            val radius = (age * 70 / bu[4]).toInt()
            val lum = (255 - age * 255 / bu[4]).toInt()
            for (i in 0 until N) {
                val dx = X[i] - bu[0].toInt()
                val dy = Y[i] - bu[1].toInt()
                val d = sqrt((dx * dx + dy * dy).toDouble()).toInt()
                if (abs(d - radius) < 9) {
                    val sat = if (rand() % 4 == 0) 60 else 230
                    frame[i] = add(frame[i], hsv(bu[2].toInt(), sat, lum))
                }
            }
        }
    }

    private fun frost(t: Long) {
        val cycle = (t % 8000).toInt()
        val front = when {
            cycle < 4000 -> 120 - cycle * 120 / 4000
            cycle < 6000 -> 0
            else -> (cycle - 6000) * 120 / 2000
        }
        fade(225)
        for (i in 0 until N) {
            var base = rgb(4, 10, 30)
            if (R[i] >= front) {
                val shimmer = tri((t / 9 + A[i] * 3 + R[i]).toInt())
                base = mix(rgb(40, 110, 220), rgb(170, 220, 255), shimmer)
            }
            var c = add(frame[i], scale(base, 60))
            if (R[i] >= front && rand() % 90 == 0) c = rgb(255, 255, 255)
            frame[i] = c
        }
    }

    private fun twinkle() {
        val base = intArrayOf(60, 35, 6)
        for (i in 0 until N) {
            val c = frame[i]
            var r = (c shr 16) and 0xFF
            var g = (c shr 8) and 0xFF
            var b = c and 0xFF
            r -= (r - base[0]) / 12
            g -= (g - base[1]) / 12
            b -= (b - base[2]) / 12
            frame[i] = rgb(r, g, b)
        }
        repeat(2) {
            val r = rand()
            if (r and 0x7 == 0) frame[(r ushr 8) % N] = rgb(255, 240, 200)
        }
    }

    private fun comet(t: Long) {
        val head = (t * 48 / 1600 % 48).toInt()
        val breath = tri((t / 12).toInt())
        for (i in 0 until N) {
            frame[i] = if (G[i] == 0) {
                val d1 = (head + 48 - S[i]) % 48
                val d2 = (head + 24 + 48 - S[i]) % 48
                val d = minOf(d1, d2)
                val lvl = if (d < 8) 255 - d * 32 else 0
                scale(rgb(200, 220, 255), lvl)
            } else {
                scale(rgb(255, 150, 20), 40 + breath * 3 / 4)
            }
        }
    }

    private fun rainbow(t: Long) {
        val rot = (t / 16).toInt()
        for (i in 0 until N) frame[i] = hsv((A[i] + R[i] / 2 - rot) and 0xFF, 255, 255)
    }

    /** "Your video": a soft moving gradient standing in for the stored frames. */
    private fun flashPlaceholder(t: Long) {
        for (i in 0 until N) frame[i] = hsv((Y[i] + (t / 20).toInt()) and 0xFF, 120, 170)
    }

    private fun fade(keep: Int) {
        for (i in 0 until N) frame[i] = scale(frame[i], keep)
    }

    companion object {
        private const val N = StarLayout.FACE_LEDS
        private const val FLAKES = 14
        private val X = StarLayout.xi
        private val Y = StarLayout.yi
        private val R = StarLayout.r
        private val A = StarLayout.angle
        private val G = StarLayout.group
        private val S = StarLayout.step

        fun tri(p: Int): Int {
            val q = p and 0xFF
            return if (q < 128) q * 2 else (255 - q) * 2
        }

        fun rgb(r: Int, g: Int, b: Int) = ((r and 0xFF) shl 16) or ((g and 0xFF) shl 8) or (b and 0xFF)

        fun scale(c: Int, level: Int): Int {
            val l = level.coerceIn(0, 255)
            return rgb(
                (((c shr 16) and 0xFF) * l + 127) / 255,
                (((c shr 8) and 0xFF) * l + 127) / 255,
                ((c and 0xFF) * l + 127) / 255,
            )
        }

        fun add(a: Int, b: Int) = rgb(
            minOf(255, ((a shr 16) and 0xFF) + ((b shr 16) and 0xFF)),
            minOf(255, ((a shr 8) and 0xFF) + ((b shr 8) and 0xFF)),
            minOf(255, (a and 0xFF) + (b and 0xFF)),
        )

        fun mix(a: Int, b: Int, t: Int): Int {
            fun ch(x: Int, y: Int) = x + ((y - x) * t) / 255
            return rgb(
                ch((a shr 16) and 0xFF, (b shr 16) and 0xFF),
                ch((a shr 8) and 0xFF, (b shr 8) and 0xFF),
                ch(a and 0xFF, b and 0xFF),
            )
        }

        fun hsv(h: Int, s: Int, v: Int): Int {
            val region = h / 43
            val rem = (h - region * 43) * 6
            val p = v * (255 - s) / 255
            val q = v * (255 - s * rem / 255) / 255
            val t = v * (255 - s * (255 - rem) / 255) / 255
            return when (region) {
                0 -> rgb(v, t, p)
                1 -> rgb(q, v, p)
                2 -> rgb(p, v, t)
                3 -> rgb(p, q, v)
                4 -> rgb(t, p, v)
                else -> rgb(v, p, q)
            }
        }
    }
}
