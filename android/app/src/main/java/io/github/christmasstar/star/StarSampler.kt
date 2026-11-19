package io.github.christmasstar.star

import kotlin.math.roundToInt

/**
 * Samples the colours under the 66 front LEDs from a frame (a photo or video
 * frame, possibly downscaled). The back face copies the front.
 */
object StarSampler {
    /** Radius of the sampled area around each LED, in star millimetres. */
    private const val SPOT_MM = 3.5f

    /**
     * @param pixels frame pixels (ARGB), [width] x [height]
     * @param transform how the source is shown in the box
     * @param star the star drawn over the same box
     * @return 66 colours 0xRRGGBB
     */
    fun sample(
        pixels: IntArray,
        width: Int,
        height: Int,
        transform: ContentTransform,
        star: StarGeometry,
    ): IntArray {
        val out = IntArray(StarLayout.FACE_LEDS)
        // Frame pixels per source pixel (frames may be downscaled).
        val fx = width.toFloat() / transform.srcWidth
        val fy = height.toFloat() / transform.srcHeight
        // Spot radius in frame pixels.
        val spot = (SPOT_MM * star.pxPerMm / (transform.scale * transform.fit) * fx).coerceAtLeast(0.5f)
        val step = maxOf(1, (spot / 1.5f).roundToInt())
        val reach = maxOf(1, spot.roundToInt())

        for (i in 0 until StarLayout.FACE_LEDS) {
            val src = transform.sourceAt(star.led(i))
            val cx = (src.x * fx).roundToInt()
            val cy = (src.y * fy).roundToInt()
            var r = 0
            var g = 0
            var b = 0
            var n = 0
            var dy = -reach
            while (dy <= reach) {
                var dx = -reach
                while (dx <= reach) {
                    val x = cx + dx
                    val y = cy + dy
                    if (x in 0 until width && y in 0 until height) {
                        val c = pixels[y * width + x]
                        r += (c shr 16) and 0xFF
                        g += (c shr 8) and 0xFF
                        b += c and 0xFF
                    }
                    // Outside the picture counts as black.
                    n++
                    dx += step
                }
                dy += step
            }
            out[i] = ((r / n) shl 16) or ((g / n) shl 8) or (b / n)
        }
        return out
    }

    /** Expand 66 front colours to all 132 LEDs (back copies front). */
    fun bothFaces(front: IntArray): IntArray = IntArray(StarLayout.LED_COUNT) { i ->
        if (i < StarLayout.FACE_LEDS) front[i] else front[StarLayout.partner[i]]
    }
}
