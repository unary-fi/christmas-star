package io.github.christmasstar.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.GridItemSpan
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Slider
import androidx.compose.material3.SliderDefaults
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableLongStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.runtime.withFrameMillis
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import io.github.christmasstar.effects.EFFECTS
import io.github.christmasstar.effects.EffectInfo
import io.github.christmasstar.effects.EffectPreview
import io.github.christmasstar.protocol.PreviewClip
import io.github.christmasstar.star.StarLayout
import io.github.christmasstar.star.turning
import io.github.christmasstar.star.StarCanvas
import io.github.christmasstar.ui.theme.StarColors

@Composable
fun EffectsScreen(vm: StarViewModel) {
    val selected by vm.effect.collectAsStateWithLifecycle()
    val speed by vm.speed.collectAsStateWithLifecycle()
    val star by vm.star.collectAsStateWithLifecycle()
    val effects by vm.effects.collectAsStateWithLifecycle()
    val clips by vm.clips.collectAsStateWithLifecycle()
    // Fetch the star's preview clips while this page is open.
    LaunchedEffect(effects) { vm.loadPreviews() }

    // Shared clock for all previews, scaled by the effect speed like on the star.
    var effectTime by remember { mutableLongStateOf(0L) }
    LaunchedEffect(speed) {
        var last = withFrameMillis { it }
        while (true) {
            val now = withFrameMillis { it }
            effectTime += (now - last) * speed / 100
            last = now
        }
    }

    val items = remember(star?.flashName, effects) {
        val video = star?.flashName?.let { EffectInfo("flash", "Your video", it) }
        listOfNotNull(video) + effects
    }

    LazyVerticalGrid(
        columns = GridCells.Fixed(2),
        modifier = Modifier.fillMaxSize(),
        contentPadding = PaddingValues(horizontal = 16.dp, vertical = 8.dp),
        horizontalArrangement = Arrangement.spacedBy(12.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        item(span = { GridItemSpan(2) }) {
            Column(Modifier.padding(horizontal = 4.dp)) {
                Text(
                    "Shake the star to knock its lights loose — it works with every effect.",
                    style = MaterialTheme.typography.bodySmall,
                    color = StarColors.TextMuted,
                )
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text("Speed", style = MaterialTheme.typography.labelLarge, color = StarColors.Text)
                    Slider(
                        value = speed.toFloat(),
                        onValueChange = { vm.setSpeed(it.toInt()) },
                        valueRange = 25f..400f,
                        modifier = Modifier.weight(1f).padding(horizontal = 12.dp),
                        colors = SliderDefaults.colors(
                            thumbColor = StarColors.GoldSoft,
                            activeTrackColor = StarColors.Gold,
                            inactiveTrackColor = StarColors.Glass,
                        ),
                    )
                    Text("$speed %", style = MaterialTheme.typography.labelLarge, color = StarColors.TextMuted)
                }
            }
        }
        items(items, key = { it.id }) { fx ->
            EffectCard(fx, clips[fx.id], selected == fx.id, { effectTime }) { vm.playEffect(fx.id) }
        }
        item(span = { GridItemSpan(2) }) { Spacer(Modifier.height(8.dp)) }
    }
}

@Composable
private fun EffectCard(
    fx: EffectInfo,
    clip: PreviewClip?,
    selected: Boolean,
    time: () -> Long,
    onClick: () -> Unit,
) {
    // The star's own clip when it has arrived; until then the app's built-in
    // preview, or the plain outline for an effect the app does not know.
    val builtIn = remember(fx.id) {
        if (fx.id == "flash" || EFFECTS.any { it.id == fx.id }) EffectPreview(fx.id) else null
    }
    var colors by remember { mutableStateOf<IntArray?>(null) }
    var turn by remember { mutableFloatStateOf(1f) }
    LaunchedEffect(fx.id, clip) {
        while (true) {
            val now = withFrameMillis { it }
            val frame = clip?.frameAt(time()) ?: builtIn?.render(time())?.copyOf()
            if (frame != null && frame.size == StarLayout.LED_COUNT) {
                // Two different faces: the card turns slowly to show both.
                val (seen, sx) = turning(frame, now)
                colors = seen
                turn = sx
            } else {
                colors = frame
                turn = 1f
            }
        }
    }
    GlassCard(Modifier.fillMaxWidth(), selected = selected, onClick = onClick) {
        Column(Modifier.padding(12.dp), horizontalAlignment = Alignment.CenterHorizontally) {
            StarCanvas(
                colors,
                Modifier.fillMaxWidth().aspectRatio(1f),
                outlineAlpha = 0.3f + 0.4f * kotlin.math.abs(turn),
                glow = 0.55f + 0.45f * kotlin.math.abs(turn),
                scaleX = turn,
            )
            Spacer(Modifier.height(6.dp))
            Text(
                fx.title,
                style = MaterialTheme.typography.titleMedium,
                color = if (selected) StarColors.Gold else StarColors.Text,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
            val tags = listOfNotNull("TILT".takeIf { fx.tilt }, "TWO FACES".takeIf { fx.twoFaces })
            if (tags.isNotEmpty()) {
                Text(
                    tags.joinToString("  ·  "),
                    style = MaterialTheme.typography.labelSmall,
                    color = StarColors.Gold,
                )
            }
            Text(
                fx.subtitle,
                style = MaterialTheme.typography.bodySmall,
                color = StarColors.TextMuted,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
        }
    }
}
