package io.github.christmasstar.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import io.github.christmasstar.star.StarCanvas
import io.github.christmasstar.star.StarLayout
import io.github.christmasstar.ui.theme.StarColors

/** A few festive colours to start from. */
private val PRESETS = listOf(
    0xFFB040 to "Gold",
    0xFFE2C0 to "Warm white",
    0xE0182A to "Red",
    0x10B040 to "Green",
    0x7FC8FF to "Ice",
    0x8A4DFF to "Violet",
)

@Composable
fun ManualScreen(vm: StarViewModel) {
    val front by vm.front.collectAsStateWithLifecycle()
    val back by vm.back.collectAsStateWithLifecycle()
    val target by vm.target.collectAsStateWithLifecycle()

    // Opening the page shows the chosen colours on the star.
    LaunchedEffect(Unit) { vm.applyManual() }

    val edited = if (target == Target.Back) back else front
    val frontColors = remember(front) { IntArray(StarLayout.FACE_LEDS) { front } }
    val backColors = remember(back) { IntArray(StarLayout.FACE_LEDS) { back } }

    Column(
        Modifier.fillMaxSize().padding(horizontal = 20.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        // The two faces.
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceEvenly) {
            FacePreview("Front", frontColors, selected = target != Target.Back, Modifier.weight(1f))
            FacePreview("Back", backColors, selected = target != Target.Front, Modifier.weight(1f))
        }
        Spacer(Modifier.height(10.dp))
        Segmented(
            options = listOf(Target.Front to "Front", Target.Both to "Both", Target.Back to "Back"),
            selected = target,
            onSelect = vm::setTarget,
        )
        Spacer(Modifier.height(14.dp))
        Text(
            "#%06X".format(edited),
            style = MaterialTheme.typography.headlineSmall,
            color = StarColors.GoldSoft,
        )
        Spacer(Modifier.height(10.dp))
        GlassCard(Modifier.fillMaxWidth().weight(1f)) {
            Row(
                Modifier.fillMaxSize().padding(vertical = 16.dp),
                horizontalArrangement = Arrangement.SpaceEvenly,
            ) {
                ChannelSlider((edited shr 16) and 0xFF, StarColors.Red, "R", { vm.setChannel(0, it) })
                ChannelSlider((edited shr 8) and 0xFF, StarColors.Green, "G", { vm.setChannel(1, it) })
                ChannelSlider(edited and 0xFF, StarColors.Blue, "B", { vm.setChannel(2, it) })
            }
        }
        Spacer(Modifier.height(12.dp))
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
            PRESETS.forEach { (rgb, name) ->
                Column(horizontalAlignment = Alignment.CenterHorizontally) {
                    Box(
                        Modifier
                            .size(34.dp)
                            .clip(CircleShape)
                            .background(Color(0xFF000000.toInt() or rgb))
                            .border(1.dp, StarColors.GlassBorder, CircleShape)
                            .clickable { vm.setColor(rgb) },
                    )
                    Text(
                        name,
                        style = MaterialTheme.typography.labelMedium,
                        color = StarColors.TextMuted,
                        textAlign = TextAlign.Center,
                        modifier = Modifier.padding(top = 4.dp),
                    )
                }
            }
        }
        Spacer(Modifier.height(8.dp))
    }
}

@Composable
private fun FacePreview(label: String, colors: IntArray, selected: Boolean, modifier: Modifier) {
    Column(modifier, horizontalAlignment = Alignment.CenterHorizontally) {
        StarCanvas(
            colors,
            Modifier.fillMaxWidth(0.92f).aspectRatio(1f),
            outlineAlpha = if (selected) 0.75f else 0.3f,
            glow = if (selected) 1f else 0.5f,
        )
        Text(
            label,
            style = MaterialTheme.typography.titleMedium,
            color = if (selected) StarColors.Gold else StarColors.TextMuted,
        )
    }
}
