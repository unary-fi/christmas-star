package io.github.christmasstar.ui.theme

import androidx.compose.animation.core.LinearEasing
import androidx.compose.animation.core.RepeatMode
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.core.tween
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import kotlin.math.PI
import kotlin.math.sin
import kotlin.random.Random

private class SkyStar(val x: Float, val y: Float, val r: Float, val phase: Float, val speed: Float, val bright: Float)

/**
 * The background: a night-sky gradient with a sparse field of tiny, slowly
 * twinkling stars, kept faint so content stays readable.
 */
@Composable
fun NightSky(modifier: Modifier = Modifier, content: @Composable () -> Unit) {
    val stars = remember {
        val rnd = Random(2024)
        List(70) {
            SkyStar(
                x = rnd.nextFloat(),
                y = rnd.nextFloat(),
                r = 0.5f + rnd.nextFloat() * rnd.nextFloat() * 1.3f,
                phase = rnd.nextFloat() * 2f * PI.toFloat(),
                speed = 0.4f + rnd.nextFloat() * 0.8f,
                bright = 0.15f + rnd.nextFloat() * 0.45f,
            )
        }
    }
    val time by rememberInfiniteTransition(label = "sky").animateFloat(
        initialValue = 0f,
        targetValue = 2f * PI.toFloat() * 10f,
        animationSpec = infiniteRepeatable(tween(60_000, easing = LinearEasing), RepeatMode.Restart),
        label = "twinkle",
    )
    Box(
        modifier
            .fillMaxSize()
            .background(
                Brush.verticalGradient(
                    listOf(StarColors.NightTop, StarColors.NightMid, StarColors.NightBottom),
                ),
            ),
    ) {
        Canvas(Modifier.fillMaxSize()) {
            val density = density
            for (s in stars) {
                val tw = 0.55f + 0.45f * sin(time * s.speed + s.phase)
                drawCircle(
                    Color.White.copy(alpha = (s.bright * tw).coerceIn(0f, 1f)),
                    radius = s.r * density,
                    center = Offset(s.x * size.width, s.y * size.height),
                )
            }
        }
        content()
    }
}
