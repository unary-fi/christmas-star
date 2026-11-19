package io.github.christmasstar.ui

import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.StrokeJoin
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.hapticfeedback.HapticFeedbackType
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalHapticFeedback
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.compose.LifecycleEventEffect
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import io.github.christmasstar.ble.StarConnection
import io.github.christmasstar.protocol.GameMode
import io.github.christmasstar.protocol.GameStatus
import io.github.christmasstar.ui.theme.StarColors
import kotlinx.coroutines.delay

/** Comet: the star runs the game, this page steers it. */
@Composable
fun PlayScreen(vm: StarViewModel) {
    val conn by vm.connection.state.collectAsStateWithLifecycle()
    val access by vm.access.collectAsStateWithLifecycle()
    val proto by vm.proto.collectAsStateWithLifecycle()
    val game by vm.game.collectAsStateWithLifecycle()
    val ready = conn == StarConnection.State.Ready && access != null
    val haptic = LocalHapticFeedback.current

    // Poll the star's status: often while playing, rarely otherwise.
    val running by rememberUpdatedState(game?.running == true)
    LaunchedEffect(ready, proto) {
        if (ready && proto >= 4) {
            while (true) {
                vm.pollGame()
                delay(if (running) 300 else 1500)
            }
        }
    }
    // Leaving the page or the app pauses a running game.
    val pause = {
        val s = vm.game.value?.state
        if (s == "play" || s == "countdown") vm.pauseGame(true)
    }
    DisposableEffect(Unit) { onDispose { pause() } }
    LifecycleEventEffect(Lifecycle.Event.ON_STOP) { pause() }

    // Haptics for what happens on the star.
    var lastScore by remember { mutableIntStateOf(-1) }
    LaunchedEffect(game?.score) {
        val score = game?.score ?: return@LaunchedEffect
        if (lastScore in 0 until score && game?.running == true) haptic.performHapticFeedback(HapticFeedbackType.Confirm)
        lastScore = score
    }
    LaunchedEffect(game?.state) {
        when (game?.state) {
            "over" -> haptic.performHapticFeedback(HapticFeedbackType.Reject)
            "won" -> haptic.performHapticFeedback(HapticFeedbackType.Confirm)
        }
    }

    val g = game
    Box(Modifier.fillMaxSize().padding(horizontal = 18.dp, vertical = 8.dp)) {
        when {
            !ready -> Notice("Connect to your star to play.")
            proto < 4 -> UpdateNeeded(vm)
            g == null || g.state == "off" -> Chooser(g, onStart = vm::startGame)
            g.ended -> EndView(g, onAgain = { vm.startGame(g.mode) }, onLeave = vm::stopGame)
            else -> GameView(g, vm)
        }
    }
}

@Composable
private fun Notice(text: String, content: @Composable () -> Unit = {}) {
    Column(
        Modifier.fillMaxSize(),
        verticalArrangement = Arrangement.Center,
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        GlyphIcon(Glyph.Comet, StarColors.Gold, Modifier.size(56.dp))
        Spacer(Modifier.height(16.dp))
        Text(text, style = MaterialTheme.typography.titleMedium, color = StarColors.Text, textAlign = TextAlign.Center)
        Spacer(Modifier.height(16.dp))
        content()
    }
}

@Composable
private fun UpdateNeeded(vm: StarViewModel) {
    Notice("Comet needs newer firmware on the star (1.6 or later).") {
        if (vm.firmwareUpdateAvailable()) {
            GoldButton("Update the star") { vm.updateFirmware() }
        } else {
            Caption("Update the app, then the star, to play.")
        }
    }
}

@Composable
private fun Chooser(g: GameStatus?, onStart: (GameMode) -> Unit) {
    var mode by rememberSaveable { mutableStateOf(g?.mode ?: GameMode.Single) }
    Column(Modifier.fillMaxSize(), verticalArrangement = Arrangement.spacedBy(14.dp)) {
        Spacer(Modifier.height(4.dp))
        Text("Comet", style = MaterialTheme.typography.displaySmall, color = StarColors.GoldSoft)
        Text(
            "Steer a comet along the star's lights. Catch the golden sparks to grow — " +
                "but never run into your own tail.",
            style = MaterialTheme.typography.bodyMedium,
            color = StarColors.TextMuted,
        )
        ModeCard(
            title = "One side",
            text = "The game stays on the front and shows on the back too: everyone around the star sees the same game.",
            best = g?.bestSingle ?: 0,
            selected = mode == GameMode.Single,
        ) { mode = GameMode.Single }
        ModeCard(
            title = "Both sides",
            text = "Flip the comet through the star at any moment. Sparks appear on either side — " +
                "run around the star to find them. Sparks count double.",
            best = g?.bestDouble ?: 0,
            selected = mode == GameMode.Double,
        ) { mode = GameMode.Double }
        Spacer(Modifier.weight(1f))
        GoldButton("Start", Modifier.fillMaxWidth()) { onStart(mode) }
        Spacer(Modifier.height(4.dp))
    }
}

@Composable
private fun ModeCard(title: String, text: String, best: Int, selected: Boolean, onClick: () -> Unit) {
    GlassCard(Modifier.fillMaxWidth(), selected = selected, onClick = onClick) {
        Column(Modifier.padding(16.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text(
                    title,
                    style = MaterialTheme.typography.titleLarge,
                    color = if (selected) StarColors.Gold else StarColors.Text,
                    modifier = Modifier.weight(1f),
                )
                if (best > 0) Text("Best $best", style = MaterialTheme.typography.labelLarge, color = StarColors.GoldSoft)
            }
            Spacer(Modifier.height(4.dp))
            Text(text, style = MaterialTheme.typography.bodySmall, color = StarColors.TextMuted)
        }
    }
}

@Composable
private fun GameView(g: GameStatus, vm: StarViewModel) {
    val double = g.mode == GameMode.Double
    Column(Modifier.fillMaxSize(), verticalArrangement = Arrangement.spacedBy(12.dp)) {
        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.Bottom) {
            Column(Modifier.weight(1f)) {
                Text("${g.score}", style = MaterialTheme.typography.displaySmall, color = StarColors.GoldSoft)
                Caption("SCORE")
            }
            Stat("${g.level}", "LEVEL")
            Spacer(Modifier.size(18.dp))
            Stat("${g.length}", "LENGTH")
            if (double) {
                Spacer(Modifier.size(18.dp))
                Stat(if (g.face == "back") "Back" else "Front", "COMET")
            }
        }
        Text(
            when (g.state) {
                "countdown" -> "Get ready…"
                "paused" -> "Paused"
                else -> if (double) "Turn where the white light shows, or flip through the star." else "Turn where the white light shows."
            },
            style = MaterialTheme.typography.bodyMedium,
            color = if (g.state == "play") StarColors.TextMuted else StarColors.Gold,
        )
        Row(
            Modifier.fillMaxWidth().weight(1f),
            horizontalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            Pad(Modifier.weight(1f), Arrow.Left) { vm.steer("l") }
            if (double) Pad(Modifier.weight(0.8f), Arrow.Flip) { vm.steer("f") }
            Pad(Modifier.weight(1f), Arrow.Right) { vm.steer("r") }
        }
        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
            val paused = g.state == "paused"
            OutlinedButton(
                onClick = { vm.pauseGame(!paused) },
                border = BorderStroke(1.dp, StarColors.GlassBorder),
            ) {
                GlyphIcon(if (paused) Glyph.Play else Glyph.Pause, StarColors.Text, Modifier.size(18.dp))
                Spacer(Modifier.size(8.dp))
                Text(if (paused) "Resume" else "Pause", color = StarColors.Text)
            }
            Spacer(Modifier.weight(1f))
            TextButton(onClick = vm::stopGame) { Text("Back to effect", color = StarColors.TextMuted) }
        }
    }
}

@Composable
private fun Stat(value: String, label: String) {
    Column(horizontalAlignment = Alignment.CenterHorizontally) {
        Text(value, style = MaterialTheme.typography.titleLarge, color = StarColors.Text)
        Caption(label)
    }
}

private enum class Arrow { Left, Right, Flip }

/** A large steering pad: reacts on touch-down, for the lowest delay. */
@Composable
private fun Pad(modifier: Modifier, arrow: Arrow, onPress: () -> Unit) {
    val haptic = LocalHapticFeedback.current
    var pressed by remember { mutableStateOf(false) }
    val glow by animateFloatAsState(if (pressed) 1f else 0f, label = "pad")
    val shape = RoundedCornerShape(28.dp)
    val press by rememberUpdatedState(onPress)
    Box(
        modifier
            .fillMaxHeight()
            .clip(shape)
            .background(
                Brush.verticalGradient(
                    listOf(
                        StarColors.Gold.copy(alpha = 0.06f + 0.22f * glow),
                        Color.White.copy(alpha = 0.03f),
                    ),
                ),
            )
            .border(1.dp, StarColors.Gold.copy(alpha = 0.25f + 0.6f * glow), shape)
            .pointerInput(Unit) {
                detectTapGestures(onPress = {
                    pressed = true
                    haptic.performHapticFeedback(HapticFeedbackType.VirtualKey)
                    press()
                    tryAwaitRelease()
                    pressed = false
                })
            },
        contentAlignment = Alignment.Center,
    ) {
        Column(horizontalAlignment = Alignment.CenterHorizontally) {
            val tint = if (pressed) StarColors.GoldSoft else StarColors.Gold
            Canvas(Modifier.size(64.dp)) {
                val w = size.width
                val h = size.height
                val s = Stroke(width = w * 0.09f, cap = StrokeCap.Round, join = StrokeJoin.Round)
                when (arrow) {
                    Arrow.Left -> drawPath(Path().apply {
                        moveTo(w * 0.62f, h * 0.2f); lineTo(w * 0.32f, h * 0.5f); lineTo(w * 0.62f, h * 0.8f)
                    }, tint, style = s)
                    Arrow.Right -> drawPath(Path().apply {
                        moveTo(w * 0.38f, h * 0.2f); lineTo(w * 0.68f, h * 0.5f); lineTo(w * 0.38f, h * 0.8f)
                    }, tint, style = s)
                    Arrow.Flip -> {
                        // Through the star: a line through a disc, arrows on both ends.
                        drawCircle(tint.copy(alpha = 0.35f), w * 0.2f, Offset(w / 2, h / 2))
                        drawLine(tint, Offset(w * 0.5f, h * 0.12f), Offset(w * 0.5f, h * 0.88f), s.width, StrokeCap.Round)
                        drawPath(Path().apply {
                            moveTo(w * 0.34f, h * 0.28f); lineTo(w * 0.5f, h * 0.12f); lineTo(w * 0.66f, h * 0.28f)
                            moveTo(w * 0.34f, h * 0.72f); lineTo(w * 0.5f, h * 0.88f); lineTo(w * 0.66f, h * 0.72f)
                        }, tint, style = s)
                    }
                }
            }
            Text(
                when (arrow) {
                    Arrow.Left -> "LEFT"
                    Arrow.Right -> "RIGHT"
                    Arrow.Flip -> "FLIP"
                },
                style = MaterialTheme.typography.labelMedium,
                color = StarColors.TextMuted,
            )
        }
    }
}

@Composable
private fun EndView(g: GameStatus, onAgain: () -> Unit, onLeave: () -> Unit) {
    val best = g.best(g.mode)
    Column(
        Modifier.fillMaxSize(),
        verticalArrangement = Arrangement.Center,
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Text(
            if (g.state == "won") "The star is full of light!" else "Game over",
            style = MaterialTheme.typography.displaySmall,
            color = StarColors.GoldSoft,
            textAlign = TextAlign.Center,
        )
        Spacer(Modifier.height(12.dp))
        Text("${g.score}", style = MaterialTheme.typography.displaySmall, color = StarColors.Text)
        Caption("SCORE")
        Spacer(Modifier.height(8.dp))
        Text(
            if (g.score > 0 && g.score >= best) "A new best!" else "Best $best",
            style = MaterialTheme.typography.titleMedium,
            color = StarColors.Gold,
        )
        Spacer(Modifier.height(28.dp))
        GoldButton("Play again", Modifier.fillMaxWidth()) { onAgain() }
        Spacer(Modifier.height(4.dp))
        TextButton(onClick = onLeave) { Text("Back to effect", color = StarColors.TextMuted) }
    }
}
