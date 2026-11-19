package io.github.christmasstar.star

import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import kotlin.math.min

/**
 * Maps star millimetres (origin at the centre, y up) to a drawing area.
 * The PCB outline reaches 125 mm; a small margin keeps the glow inside.
 */
class StarGeometry(val size: Size) {
    val center = Offset(size.width / 2f, size.height / 2f)
    val pxPerMm = min(size.width, size.height) / (2f * EXTENT_MM)

    fun toScreen(xMm: Float, yMm: Float) = Offset(center.x + xMm * pxPerMm, center.y - yMm * pxPerMm)

    fun led(i: Int) = toScreen(StarLayout.x[i], StarLayout.y[i])

    companion object {
        const val EXTENT_MM = 132f
    }
}

/**
 * How a photo or video frame (source pixels) is shown: fitted and centred in
 * the box, then scaled around the box centre and moved by the user.
 */
data class ContentTransform(
    val srcWidth: Int,
    val srcHeight: Int,
    val boxWidth: Float,
    val boxHeight: Float,
    val scale: Float = 1f,
    val offset: Offset = Offset.Zero,
) {
    val fit: Float get() = min(boxWidth / srcWidth, boxHeight / srcHeight)

    /** Source pixel shown at a box position. */
    fun sourceAt(p: Offset): Offset {
        val k = scale * fit
        return Offset(
            srcWidth / 2f + (p.x - boxWidth / 2f - offset.x) / k,
            srcHeight / 2f + (p.y - boxHeight / 2f - offset.y) / k,
        )
    }

    /** Box position of a source pixel. */
    fun boxAt(src: Offset): Offset {
        val k = scale * fit
        return Offset(
            boxWidth / 2f + offset.x + (src.x - srcWidth / 2f) * k,
            boxHeight / 2f + offset.y + (src.y - srcHeight / 2f) * k,
        )
    }
}
