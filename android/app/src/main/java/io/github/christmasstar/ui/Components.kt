package io.github.christmasstar.ui

import androidx.compose.animation.animateColorAsState
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxScope
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.StrokeJoin
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import io.github.christmasstar.ui.theme.StarColors
import kotlin.math.PI
import kotlin.math.cos
import kotlin.math.sin

/** A rounded, slightly translucent card with a subtle border. */
@Composable
fun GlassCard(
    modifier: Modifier = Modifier,
    selected: Boolean = false,
    onClick: (() -> Unit)? = null,
    content: @Composable BoxScope.() -> Unit,
) {
    val border by animateColorAsState(
        if (selected) StarColors.Gold.copy(alpha = 0.85f) else StarColors.GlassBorder, label = "border",
    )
    val shape = RoundedCornerShape(22.dp)
    Box(
        modifier
            .clip(shape)
            .background(
                Brush.verticalGradient(
                    listOf(Color.White.copy(alpha = if (selected) 0.10f else 0.07f), Color.White.copy(alpha = 0.03f)),
                ),
            )
            .border(BorderStroke(if (selected) 1.5.dp else 1.dp, border), shape)
            .let { if (onClick != null) it.clickable(onClick = onClick) else it },
        content = content,
    )
}

/**
 * A tall vertical slider for one colour channel (0..255).
 */
@Composable
fun ChannelSlider(
    value: Int,
    color: Color,
    label: String,
    onChange: (Int) -> Unit,
    modifier: Modifier = Modifier,
    width: Dp = 64.dp,
) {
    val current by rememberUpdatedState(value)
    val change by rememberUpdatedState(onChange)
    Box(
        modifier
            .fillMaxHeight()
            .width(width)
            .pointerInput(Unit) {
                fun set(y: Float) {
                    val v = ((1f - y / size.height) * 255f).toInt().coerceIn(0, 255)
                    if (v != current) change(v)
                }
                detectTapGestures { set(it.y) }
            }
            .pointerInput(Unit) {
                fun set(y: Float) {
                    val v = ((1f - y / size.height) * 255f).toInt().coerceIn(0, 255)
                    if (v != current) change(v)
                }
                detectDragGestures(onDragStart = { set(it.y) }) { change, _ -> set(change.position.y) }
            },
    ) {
        Canvas(Modifier.matchParentSize()) {
            val trackW = size.width * 0.42f
            val left = (size.width - trackW) / 2f
            val r = CornerRadius(trackW / 2f)
            drawRoundRect(StarColors.Glass, Offset(left, 0f), Size(trackW, size.height), r)
            val frac = value / 255f
            val fillH = size.height * frac
            drawRoundRect(
                Brush.verticalGradient(
                    listOf(color, color.copy(alpha = 0.25f)),
                    startY = size.height - fillH,
                    endY = size.height,
                ),
                Offset(left, size.height - fillH),
                Size(trackW, fillH),
                r,
            )
            drawRoundRect(StarColors.GlassBorder, Offset(left, 0f), Size(trackW, size.height), r, style = Stroke(1.dp.toPx()))
            // Thumb with a soft glow.
            val y = (size.height - fillH).coerceIn(trackW / 2f, size.height - trackW / 2f)
            val c = Offset(size.width / 2f, y)
            drawCircle(Brush.radialGradient(listOf(color.copy(alpha = 0.6f), Color.Transparent), c, trackW * 1.3f), trackW * 1.3f, c)
            drawCircle(Color.White, trackW * 0.62f, c)
            drawCircle(color, trackW * 0.42f, c)
        }
        Text(
            "$label\n$value",
            style = MaterialTheme.typography.labelMedium,
            color = StarColors.Text,
            modifier = Modifier.align(Alignment.TopCenter).padding(top = 4.dp),
            textAlign = androidx.compose.ui.text.style.TextAlign.Center,
        )
    }
}

/** Segmented control with a gold selection pill. */
@Composable
fun <T> Segmented(options: List<Pair<T, String>>, selected: T, onSelect: (T) -> Unit, modifier: Modifier = Modifier) {
    val shape = RoundedCornerShape(50)
    Row(
        modifier
            .clip(shape)
            .background(StarColors.Glass)
            .border(1.dp, StarColors.GlassBorder, shape)
            .padding(4.dp),
    ) {
        options.forEach { (value, label) ->
            val on = value == selected
            val bg by animateColorAsState(if (on) StarColors.Gold else Color.Transparent, label = "seg")
            Box(
                Modifier
                    .clip(shape)
                    .background(bg)
                    .clickable { onSelect(value) }
                    .padding(horizontal = 18.dp, vertical = 8.dp),
            ) {
                Text(
                    label,
                    style = MaterialTheme.typography.labelLarge,
                    color = if (on) Color(0xFF241A00) else StarColors.Text,
                )
            }
        }
    }
}

// ---- Small line glyphs (no icon library needed) ----------------------------------

enum class Glyph { Sliders, Picture, Sparkle, Star, Play, Pause, Sun, Shake, Bluetooth, Upload, Comet }

@Composable
fun GlyphIcon(glyph: Glyph, tint: Color, modifier: Modifier = Modifier) {
    Canvas(modifier.size(24.dp)) { drawGlyph(glyph, tint) }
}

private fun DrawScope.drawGlyph(g: Glyph, c: Color) {
    val w = size.width
    val h = size.height
    val s = Stroke(width = w * 0.08f, cap = StrokeCap.Round, join = StrokeJoin.Round)
    when (g) {
        Glyph.Sliders -> {
            listOf(0.25f to 0.35f, 0.5f to 0.65f, 0.75f to 0.45f).forEach { (x, k) ->
                drawLine(c, Offset(w * x, h * 0.15f), Offset(w * x, h * 0.85f), s.width, StrokeCap.Round)
                drawCircle(c, w * 0.09f, Offset(w * x, h * k))
            }
        }
        Glyph.Picture -> {
            drawRoundRect(c, Offset(w * 0.12f, h * 0.2f), Size(w * 0.76f, h * 0.6f), CornerRadius(w * 0.08f), style = s)
            val p = Path().apply {
                moveTo(w * 0.18f, h * 0.72f); lineTo(w * 0.42f, h * 0.45f); lineTo(w * 0.58f, h * 0.62f)
                lineTo(w * 0.68f, h * 0.52f); lineTo(w * 0.82f, h * 0.72f)
            }
            drawPath(p, c, style = s)
            drawCircle(c, w * 0.06f, Offset(w * 0.66f, h * 0.36f))
        }
        Glyph.Sparkle -> {
            fun spark(cx: Float, cy: Float, r: Float) {
                val p = Path().apply {
                    moveTo(cx, cy - r); quadraticTo(cx, cy, cx + r, cy); quadraticTo(cx, cy, cx, cy + r)
                    quadraticTo(cx, cy, cx - r, cy); quadraticTo(cx, cy, cx, cy - r); close()
                }
                drawPath(p, c)
            }
            spark(w * 0.42f, h * 0.55f, w * 0.32f)
            spark(w * 0.78f, h * 0.22f, w * 0.14f)
        }
        Glyph.Star -> drawPath(hexagram(w / 2f, h / 2f, w * 0.44f), c, style = s)
        Glyph.Play -> drawPath(Path().apply {
            moveTo(w * 0.32f, h * 0.2f); lineTo(w * 0.8f, h * 0.5f); lineTo(w * 0.32f, h * 0.8f); close()
        }, c)
        Glyph.Pause -> {
            drawRoundRect(c, Offset(w * 0.28f, h * 0.2f), Size(w * 0.14f, h * 0.6f), CornerRadius(w * 0.04f))
            drawRoundRect(c, Offset(w * 0.58f, h * 0.2f), Size(w * 0.14f, h * 0.6f), CornerRadius(w * 0.04f))
        }
        Glyph.Sun -> {
            drawCircle(c, w * 0.17f, Offset(w / 2, h / 2), style = s)
            for (k in 0 until 8) {
                val a = k * PI.toFloat() / 4f
                drawLine(c, Offset(w / 2 + cos(a) * w * 0.3f, h / 2 + sin(a) * h * 0.3f),
                    Offset(w / 2 + cos(a) * w * 0.42f, h / 2 + sin(a) * h * 0.42f), s.width, StrokeCap.Round)
            }
        }
        Glyph.Shake -> {
            drawPath(hexagram(w / 2f, h * 0.42f, w * 0.26f), c, style = s)
            drawLine(c, Offset(w * 0.4f, h * 0.78f), Offset(w * 0.4f, h * 0.92f), s.width, StrokeCap.Round)
            drawLine(c, Offset(w * 0.6f, h * 0.72f), Offset(w * 0.6f, h * 0.86f), s.width, StrokeCap.Round)
        }
        Glyph.Bluetooth -> drawPath(Path().apply {
            moveTo(w * 0.3f, h * 0.32f); lineTo(w * 0.7f, h * 0.68f); lineTo(w * 0.5f, h * 0.86f)
            lineTo(w * 0.5f, h * 0.14f); lineTo(w * 0.7f, h * 0.32f); lineTo(w * 0.3f, h * 0.68f)
        }, c, style = s)
        Glyph.Comet -> {
            // A bright head with a fading tail behind it.
            drawCircle(c, w * 0.13f, Offset(w * 0.7f, h * 0.3f))
            drawLine(c, Offset(w * 0.6f, h * 0.4f), Offset(w * 0.16f, h * 0.84f), s.width, StrokeCap.Round)
            drawLine(c.copy(alpha = c.alpha * 0.6f), Offset(w * 0.5f, h * 0.26f), Offset(w * 0.22f, h * 0.54f), s.width, StrokeCap.Round)
            drawLine(c.copy(alpha = c.alpha * 0.6f), Offset(w * 0.74f, h * 0.5f), Offset(w * 0.46f, h * 0.78f), s.width, StrokeCap.Round)
        }
        Glyph.Upload -> {
            drawLine(c, Offset(w / 2, h * 0.7f), Offset(w / 2, h * 0.18f), s.width, StrokeCap.Round)
            drawPath(Path().apply {
                moveTo(w * 0.3f, h * 0.38f); lineTo(w / 2, h * 0.18f); lineTo(w * 0.7f, h * 0.38f)
            }, c, style = s)
            drawLine(c, Offset(w * 0.2f, h * 0.84f), Offset(w * 0.8f, h * 0.84f), s.width, StrokeCap.Round)
        }
    }
}

/** Six-pointed star outline centred at (cx, cy). */
fun hexagram(cx: Float, cy: Float, r: Float): Path {
    val inner = r / 1.732f
    return Path().apply {
        for (k in 0 until 12) {
            val a = (-90f + k * 30f) * PI.toFloat() / 180f
            val rr = if (k % 2 == 0) r else inner
            val x = cx + cos(a) * rr
            val y = cy + sin(a) * rr
            if (k == 0) moveTo(x, y) else lineTo(x, y)
        }
        close()
    }
}

/** Small label + value row helper used in panels. */
@Composable
fun Caption(text: String, modifier: Modifier = Modifier) {
    Text(text, style = MaterialTheme.typography.bodySmall, color = StarColors.TextMuted, modifier = modifier)
}
