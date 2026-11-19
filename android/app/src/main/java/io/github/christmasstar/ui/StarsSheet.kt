package io.github.christmasstar.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.OutlinedTextFieldDefaults
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.rememberModalBottomSheetState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.input.KeyboardCapitalization
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import io.github.christmasstar.ble.StarConnection
import io.github.christmasstar.protocol.Pairing
import io.github.christmasstar.star.StarCanvas
import io.github.christmasstar.star.StarLayout
import io.github.christmasstar.ui.theme.StarColors
import kotlinx.coroutines.delay

/**
 * The star chooser: nearby stars, pairing hints, and the connected star's
 * name and owner. In developer mode every star can be chosen.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun StarsSheet(vm: StarViewModel) {
    val show by vm.showStars.collectAsStateWithLifecycle()
    if (!show) return
    val nearby by vm.connection.nearby.collectAsStateWithLifecycle()
    val scanning by vm.connection.scanning.collectAsStateWithLifecycle()
    val state by vm.connection.state.collectAsStateWithLifecycle()
    val paired by vm.pairedAddress.collectAsStateWithLifecycle()
    val access by vm.access.collectAsStateWithLifecycle()
    val name by vm.starName.collectAsStateWithLifecycle()
    val notice by vm.notice.collectAsStateWithLifecycle()
    val busy by vm.busy.collectAsStateWithLifecycle()
    val devMode by vm.devMode.collectAsStateWithLifecycle()
    val current = vm.connection.address.takeIf { state == StarConnection.State.Ready }

    // Keep looking while the sheet is open.
    LaunchedEffect(Unit) {
        while (true) {
            vm.rescan()
            delay(8000)
        }
    }

    ModalBottomSheet(
        onDismissRequest = vm::closeStars,
        sheetState = rememberModalBottomSheetState(skipPartiallyExpanded = true),
        containerColor = Color(0xFF0B1129),
        contentColor = StarColors.Text,
        scrimColor = Color.Black.copy(alpha = 0.55f),
    ) {
        Column(
            Modifier
                .fillMaxWidth()
                .verticalScroll(rememberScrollState())
                .padding(horizontal = 20.dp)
                .navigationBarsPadding()
                .padding(bottom = 16.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text("Stars", style = MaterialTheme.typography.displaySmall, color = StarColors.GoldSoft)
                Spacer(Modifier.width(10.dp))
                if (devMode) DevChip()
                Spacer(Modifier.weight(1f))
                if (scanning) {
                    CircularProgressIndicator(Modifier.size(18.dp), color = StarColors.Gold, strokeWidth = 2.dp)
                } else {
                    TextButton(onClick = vm::rescan) { Text("Search again", color = StarColors.Gold) }
                }
            }
            Text(
                if (devMode) "Developer mode: you can control every star. Tap one to switch."
                else "Tap your star to connect. A new star pairs with this phone during its first " +
                    "minute after being plugged in.",
                style = MaterialTheme.typography.bodySmall,
                color = StarColors.TextMuted,
            )

            notice?.let { NoticeCard(it) }

            if (current != null && access != null) {
                CurrentStar(vm, name, access!!)
            }

            if (nearby.isEmpty()) {
                Text(
                    if (scanning) "Looking for stars…" else "No stars found. Is the star plugged in and close by?",
                    style = MaterialTheme.typography.bodyMedium,
                    color = StarColors.TextMuted,
                    modifier = Modifier.padding(vertical = 12.dp),
                )
            }
            nearby.forEach { s ->
                StarRow(
                    star = s,
                    isPaired = s.address == paired,
                    isCurrent = s.address == current,
                    isBusy = s.address == busy,
                    onClick = { if (s.address != current) vm.chooseStar(s.address) },
                )
            }
            if (devMode) {
                TextButton(onClick = vm::disableDevMode) {
                    Text("Turn off developer mode", color = StarColors.TextMuted)
                }
            }
        }
    }
}

@Composable
fun DevChip() {
    val shape = RoundedCornerShape(50)
    Box(
        Modifier
            .clip(shape)
            .border(1.dp, StarColors.Gold.copy(alpha = 0.6f), shape)
            .padding(horizontal = 8.dp, vertical = 2.dp),
    ) {
        Text("DEV", style = MaterialTheme.typography.labelSmall, color = StarColors.Gold)
    }
}

@Composable
private fun NoticeCard(text: String) {
    GlassCard(Modifier.fillMaxWidth(), selected = true) {
        Row(Modifier.padding(14.dp), verticalAlignment = Alignment.Top) {
            GlyphIcon(Glyph.Shake, StarColors.Gold, Modifier.size(22.dp))
            Spacer(Modifier.width(10.dp))
            Text(text, style = MaterialTheme.typography.bodyMedium, color = StarColors.Text)
        }
    }
}

@Composable
private fun StarRow(
    star: StarConnection.FoundStar,
    isPaired: Boolean,
    isCurrent: Boolean,
    isBusy: Boolean,
    onClick: () -> Unit,
) {
    GlassCard(Modifier.fillMaxWidth(), selected = isCurrent, onClick = onClick) {
        Row(Modifier.padding(12.dp), verticalAlignment = Alignment.CenterVertically) {
            val lit = if (isCurrent || isPaired) IntArray(StarLayout.FACE_LEDS) { 0xF2B040 } else null
            StarCanvas(lit, Modifier.size(52.dp), outlineAlpha = if (lit != null) 0.9f else 0.5f, glow = 0.6f)
            Spacer(Modifier.width(12.dp))
            Column(Modifier.weight(1f)) {
                Text(
                    star.name,
                    style = MaterialTheme.typography.titleLarge,
                    color = StarColors.Text,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                )
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text(
                        star.address.replace(":", "").takeLast(4),
                        style = MaterialTheme.typography.labelMedium,
                        color = StarColors.TextMuted,
                    )
                    Spacer(Modifier.width(8.dp))
                    SignalBars(star.rssi)
                    Spacer(Modifier.width(8.dp))
                    when {
                        isCurrent -> Badge("Connected", StarColors.Green)
                        isPaired -> Badge("Yours", StarColors.Gold)
                    }
                }
            }
            if (isBusy) CircularProgressIndicator(Modifier.size(22.dp), color = StarColors.Gold, strokeWidth = 2.dp)
        }
    }
}

@Composable
private fun Badge(text: String, color: Color) {
    val shape = RoundedCornerShape(50)
    Box(
        Modifier
            .clip(shape)
            .border(1.dp, color.copy(alpha = 0.55f), shape)
            .padding(horizontal = 8.dp, vertical = 1.dp),
    ) {
        Text(text, style = MaterialTheme.typography.labelSmall, color = color)
    }
}

@Composable
private fun SignalBars(rssi: Int) {
    val n = when {
        rssi > -60 -> 4
        rssi > -70 -> 3
        rssi > -80 -> 2
        else -> 1
    }
    Row(verticalAlignment = Alignment.Bottom) {
        listOf(4, 7, 10, 13).forEachIndexed { i, h ->
            Box(
                Modifier
                    .padding(end = 2.dp)
                    .size(width = 3.dp, height = h.dp)
                    .clip(RoundedCornerShape(1.dp))
                    .background(if (i < n) StarColors.TextMuted else StarColors.Glass),
            )
        }
    }
}

@Composable
private fun CurrentStar(vm: StarViewModel, name: String, access: String) {
    var editing by remember { mutableStateOf(false) }
    var text by remember(name) { mutableStateOf(name) }
    var confirmRelease by remember { mutableStateOf(false) }

    GlassCard(Modifier.fillMaxWidth()) {
        Column(Modifier.padding(14.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            Text(
                if (access == "dev") "Connected with developer access" else "Paired with this phone",
                style = MaterialTheme.typography.labelLarge,
                color = StarColors.TextMuted,
            )
            if (editing) {
                OutlinedTextField(
                    value = text,
                    onValueChange = { if (it.length <= Pairing.NAME_MAX) text = it },
                    singleLine = true,
                    label = { Text("Star name") },
                    supportingText = { Text("${text.length}/${Pairing.NAME_MAX} — what phones see when they search") },
                    isError = text.isNotEmpty() && !Pairing.validName(text),
                    keyboardOptions = KeyboardOptions(capitalization = KeyboardCapitalization.Sentences),
                    colors = OutlinedTextFieldDefaults.colors(
                        focusedBorderColor = StarColors.Gold,
                        focusedLabelColor = StarColors.Gold,
                        cursorColor = StarColors.Gold,
                    ),
                    modifier = Modifier.fillMaxWidth(),
                )
                Row {
                    TextButton(onClick = { editing = false }) { Text("Cancel", color = StarColors.TextMuted) }
                    Spacer(Modifier.weight(1f))
                    TextButton(
                        enabled = Pairing.validName(text) && text != name,
                        onClick = { vm.rename(text.trim()); editing = false },
                    ) { Text("Rename", color = StarColors.Gold) }
                }
            } else {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text(
                        name.ifEmpty { "Your star" },
                        style = MaterialTheme.typography.headlineSmall,
                        color = StarColors.Text,
                        modifier = Modifier.weight(1f),
                    )
                    TextButton(onClick = { editing = true }) { Text("Rename", color = StarColors.Gold) }
                }
                FirmwareRow(vm)
                Row {
                    Spacer(Modifier.weight(1f))
                    TextButton(onClick = {
                        if (confirmRelease) vm.unpair() else confirmRelease = true
                    }) {
                        Text(
                            if (confirmRelease) "Tap again to release" else "Release this star",
                            color = StarColors.Red,
                        )
                    }
                }
                if (confirmRelease) {
                    Text(
                        "Releasing lets another phone pair with the star. This phone will no longer " +
                            "connect to it automatically.",
                        style = MaterialTheme.typography.bodySmall,
                        color = StarColors.TextMuted,
                    )
                }
            }
        }
    }
}

/** The star's firmware version and, when the app carries a newer one, the update button. */
@Composable
private fun FirmwareRow(vm: StarViewModel) {
    val fw by vm.starFirmware.collectAsStateWithLifecycle()
    val access by vm.access.collectAsStateWithLifecycle()
    if (fw.isEmpty()) return
    val available = access != null && vm.firmwareUpdateAvailable()
    Row(verticalAlignment = Alignment.CenterVertically) {
        Text(
            "Firmware $fw",
            style = MaterialTheme.typography.bodySmall,
            color = StarColors.TextMuted,
            modifier = Modifier.weight(1f),
        )
        if (available) {
            TextButton(onClick = vm::updateFirmware) {
                Text("Update to ${vm.bundledVersion}", color = StarColors.Gold)
            }
        }
    }
}

/** Progress of a star firmware update; cannot be closed while it runs. */
@Composable
fun FirmwareDialog(vm: StarViewModel) {
    val st by vm.fwUpdate.collectAsStateWithLifecycle()
    if (!st.running && st.error == null && st.done == null) return
    AlertDialog(
        onDismissRequest = { vm.dismissFwUpdate() },
        containerColor = Color(0xFF111A3D),
        title = { Text("Star firmware", color = StarColors.GoldSoft) },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
                when {
                    st.running -> {
                        Text(st.stage, color = StarColors.Text)
                        LinearProgressIndicator(
                            progress = { st.progress },
                            modifier = Modifier.fillMaxWidth(),
                            color = StarColors.Gold,
                            trackColor = StarColors.Glass,
                        )
                        Text(
                            "Keep the phone near the star. The star restarts at the end; it goes back to " +
                                "its old firmware by itself if anything goes wrong.",
                            style = MaterialTheme.typography.bodySmall,
                            color = StarColors.TextMuted,
                        )
                    }
                    st.done != null -> Text(st.done!!, color = StarColors.Text)
                    else -> Text(st.error ?: "", color = StarColors.Red)
                }
            }
        },
        confirmButton = {
            if (!st.running) TextButton(onClick = vm::dismissFwUpdate) { Text("OK", color = StarColors.Gold) }
        },
    )
}

/** Ask for the developer key (32 hex digits) to turn on developer mode. */
@Composable
fun DevKeyDialog(vm: StarViewModel, onDismiss: () -> Unit) {
    var text by remember { mutableStateOf("") }
    var error by remember { mutableStateOf(false) }
    AlertDialog(
        onDismissRequest = onDismiss,
        containerColor = Color(0xFF111A3D),
        title = { Text("Developer mode", color = StarColors.GoldSoft) },
        text = {
            Column {
                Text(
                    "Enter the developer key (32 hex digits, from sw/dev_key.txt). With it this " +
                        "phone can control every star, whoever owns it.",
                    style = MaterialTheme.typography.bodySmall,
                    color = StarColors.TextMuted,
                )
                Spacer(Modifier.height(10.dp))
                OutlinedTextField(
                    value = text,
                    onValueChange = { text = it; error = false },
                    singleLine = true,
                    isError = error,
                    label = { Text("Developer key") },
                    colors = OutlinedTextFieldDefaults.colors(
                        focusedBorderColor = StarColors.Gold,
                        focusedLabelColor = StarColors.Gold,
                        cursorColor = StarColors.Gold,
                    ),
                )
            }
        },
        confirmButton = {
            TextButton(onClick = {
                if (vm.enableDevMode(text)) {
                    onDismiss()
                    vm.openStars()
                } else {
                    error = true
                }
            }) { Text("Turn on", color = StarColors.Gold) }
        },
        dismissButton = { TextButton(onClick = onDismiss) { Text("Cancel", color = StarColors.TextMuted) } },
    )
}
