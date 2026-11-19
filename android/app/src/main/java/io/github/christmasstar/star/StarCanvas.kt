package io.github.christmasstar.star

import androidx.compose.foundation.Canvas
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.drawscope.scale
import io.github.christmasstar.ui.theme.StarColors
import kotlin.math.max

/**
 * The star as on the PCB: the board outline in thin gold lines and the LEDs
 * as softly glowing dots.
 *
 * @param colors 66 front colours (0xRRGGBB), or null for unlit LEDs
 * @param outlineAlpha strength of the gold outline
 */
@Composable
fun StarCanvas(
    colors: IntArray?,
    modifier: Modifier = Modifier,
    outlineAlpha: Float = 0.7f,
    glow: Float = 1f,
    scaleX: Float = 1f,
) {
    Canvas(modifier) {
        if (scaleX == 1f) {
            drawStar(colors, outlineAlpha, glow)
        } else {
            // Seen turned around the vertical axis: squeezed (negative = mirrored).
            val sx = if (scaleX < 0) minOf(scaleX, -0.02f) else maxOf(scaleX, 0.02f)
            scale(scaleX = sx, scaleY = 1f, pivot = center) { drawStar(colors, outlineAlpha, glow) }
        }
    }
}

/**
 * A two-faced frame (132 LEDs) seen on a star turning around its vertical
 * axis once per [TURN_MS]: the front while it faces us, then the back (each
 * back LED sits where its front partner is; scaleX < 0 mirrors it).
 * Returns the 66 colours to draw and the horizontal scale.
 */
fun turning(frame: IntArray, nowMs: Long): Pair<IntArray, Float> {
    val c = kotlin.math.cos(2.0 * Math.PI * (nowMs % TURN_MS) / TURN_MS).toFloat()
    val back = c < 0
    val seen = IntArray(StarLayout.FACE_LEDS) { i -> frame[if (back) StarLayout.partner[i] else i] }
    return seen to c
}

const val TURN_MS = 8000L

fun DrawScope.drawStar(colors: IntArray?, outlineAlpha: Float = 0.7f, glow: Float = 1f) {
    val g = StarGeometry(size)
    val line = max(1f, g.pxPerMm * 0.45f)
    val e = StarLayout.edges
    val gold = StarColors.Gold.copy(alpha = outlineAlpha)
    var k = 0
    while (k < e.size) {
        drawLine(
            gold,
            g.toScreen(e[k], e[k + 1]),
            g.toScreen(e[k + 2], e[k + 3]),
            strokeWidth = line,
            cap = StrokeCap.Round,
        )
        k += 4
    }

    val dot = g.pxPerMm * 2.2f
    for (i in 0 until StarLayout.FACE_LEDS) {
        val p = g.led(i)
        val rgb = colors?.get(i) ?: 0
        val c = Color(0xFF000000.toInt() or rgb)
        val lum = (0.299f * c.red + 0.587f * c.green + 0.114f * c.blue)
        if (lum > 0.02f && glow > 0f) {
            val radius = dot * (2.8f + 2.2f * lum) * glow
            drawCircle(
                Brush.radialGradient(
                    listOf(c.copy(alpha = 0.55f * glow), c.copy(alpha = 0.18f * glow), Color.Transparent),
                    center = p,
                    radius = radius,
                ),
                radius = radius,
                center = p,
            )
        }
        // The LED itself: a small bright core, or a faint ring when off.
        if (lum > 0.02f) {
            drawCircle(lighten(c), radius = dot, center = p)
        } else {
            drawCircle(StarColors.Gold.copy(alpha = 0.25f), radius = dot * 0.8f, center = p,
                style = Stroke(width = line))
        }
    }
}

private fun lighten(c: Color) = Color(
    red = c.red + (1f - c.red) * 0.35f,
    green = c.green + (1f - c.green) * 0.35f,
    blue = c.blue + (1f - c.blue) * 0.35f,
)
