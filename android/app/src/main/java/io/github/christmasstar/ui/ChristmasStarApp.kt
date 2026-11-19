package io.github.christmasstar.ui

import android.Manifest
import android.bluetooth.BluetoothAdapter
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Build
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.animation.AnimatedContent
import androidx.compose.animation.core.tween
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.togetherWith
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Slider
import androidx.compose.material3.SliderDefaults
import androidx.compose.material3.Switch
import androidx.compose.material3.SwitchDefaults
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import androidx.core.content.ContextCompat
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.viewmodel.compose.viewModel
import io.github.christmasstar.ble.StarConnection
import io.github.christmasstar.ui.theme.ChristmasStarTheme
import io.github.christmasstar.ui.theme.NightSky
import io.github.christmasstar.ui.theme.StarColors

enum class Page(val title: String, val glyph: Glyph) {
    Manual("Colour", Glyph.Sliders),
    Picture("Picture", Glyph.Picture),
    Effects("Effects", Glyph.Sparkle),
}

private val permissions: Array<String> =
    if (Build.VERSION.SDK_INT >= 31) {
        arrayOf(Manifest.permission.BLUETOOTH_SCAN, Manifest.permission.BLUETOOTH_CONNECT)
    } else {
        arrayOf(Manifest.permission.ACCESS_FINE_LOCATION)
    }

@Composable
fun ChristmasStarApp(vm: StarViewModel = viewModel()) {
    ChristmasStarTheme {
        val context = LocalContext.current
        var granted by remember {
            mutableStateOf(permissions.all {
                ContextCompat.checkSelfPermission(context, it) == PackageManager.PERMISSION_GRANTED
            })
        }
        val enableBt = rememberLauncherForActivityResult(ActivityResultContracts.StartActivityForResult()) {
            vm.connect()
        }
        val askPermissions = rememberLauncherForActivityResult(
            ActivityResultContracts.RequestMultiplePermissions(),
        ) { result ->
            granted = result.values.all { it }
            if (granted) {
                if (vm.connection.bluetoothEnabled) vm.connect()
                else enableBt.launch(Intent(BluetoothAdapter.ACTION_REQUEST_ENABLE))
            }
        }
        val connect: () -> Unit = {
            when {
                !granted -> askPermissions.launch(permissions)
                !vm.connection.bluetoothEnabled -> enableBt.launch(Intent(BluetoothAdapter.ACTION_REQUEST_ENABLE))
                else -> vm.connect()
            }
        }
        val openStars: () -> Unit = {
            when {
                !granted -> askPermissions.launch(permissions)
                !vm.connection.bluetoothEnabled -> enableBt.launch(Intent(BluetoothAdapter.ACTION_REQUEST_ENABLE))
                else -> vm.openStars()
            }
        }
        LaunchedEffect(Unit) { connect() }
        var devDialog by remember { mutableStateOf(false) }

        var page by rememberSaveable { mutableStateOf(Page.Effects) }

        NightSky {
            Column(Modifier.fillMaxSize().statusBarsPadding()) {
                TopBar(vm, onStars = openStars, onDevMode = { devDialog = true })
                Box(Modifier.weight(1f).fillMaxWidth()) {
                    AnimatedContent(
                        targetState = page,
                        transitionSpec = { fadeIn(tween(260)) togetherWith fadeOut(tween(200)) },
                        label = "page",
                    ) { p ->
                        when (p) {
                            Page.Manual -> ManualScreen(vm)
                            Page.Picture -> PictureScreen(vm)
                            Page.Effects -> EffectsScreen(vm)
                        }
                    }
                }
                BottomNav(page) { page = it }
            }
            StarsSheet(vm)
            if (devDialog) DevKeyDialog(vm) { devDialog = false }
            FirmwareDialog(vm)
        }
    }
}

@Composable
private fun TopBar(vm: StarViewModel, onStars: () -> Unit, onDevMode: () -> Unit) {
    val state by vm.connection.state.collectAsStateWithLifecycle()
    val name by vm.starName.collectAsStateWithLifecycle()
    val devMode by vm.devMode.collectAsStateWithLifecycle()
    var titleTaps by remember { mutableIntStateOf(0) }
    val error by vm.connection.error.collectAsStateWithLifecycle()
    val brightness by vm.brightness.collectAsStateWithLifecycle()
    val shake by vm.shake.collectAsStateWithLifecycle()

    Column(Modifier.fillMaxWidth().padding(horizontal = 20.dp, vertical = 8.dp)) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            GlyphIcon(Glyph.Star, StarColors.Gold, Modifier.size(26.dp))
            Spacer(Modifier.width(10.dp))
            Text(
                "Christmas Star",
                style = MaterialTheme.typography.displaySmall,
                color = StarColors.Text,
                maxLines = 1,
                // Seven taps on the title: developer mode.
                modifier = Modifier.clickable(
                    interactionSource = remember { MutableInteractionSource() },
                    indication = null,
                ) {
                    titleTaps++
                    if (titleTaps >= 7) {
                        titleTaps = 0
                        if (!devMode) onDevMode() else onStars()
                    }
                },
            )
            if (devMode) {
                Spacer(Modifier.width(8.dp))
                DevChip()
            }
            Spacer(Modifier.weight(1f))
            ConnectionChip(state, name, onStars)
        }
        if (state == StarConnection.State.Failed && error != null) {
            Caption(error!!, Modifier.align(Alignment.End).padding(top = 2.dp))
        }
        val starFw by vm.starFirmware.collectAsStateWithLifecycle()
        val access by vm.access.collectAsStateWithLifecycle()
        if (state == StarConnection.State.Ready && access != null && starFw.isNotEmpty() &&
            vm.firmwareUpdateAvailable()
        ) {
            Text(
                "Update available for your star",
                style = MaterialTheme.typography.labelMedium,
                color = StarColors.Gold,
                modifier = Modifier
                    .align(Alignment.End)
                    .padding(top = 2.dp)
                    .clickable { onStars() },
            )
        }
        Spacer(Modifier.height(10.dp))
        Row(verticalAlignment = Alignment.CenterVertically) {
            GlyphIcon(Glyph.Sun, StarColors.TextMuted, Modifier.size(20.dp))
            Slider(
                value = brightness.toFloat(),
                onValueChange = { vm.setBrightness(it.toInt()) },
                valueRange = 5f..255f,
                modifier = Modifier.weight(1f).padding(horizontal = 8.dp),
                colors = SliderDefaults.colors(
                    thumbColor = StarColors.GoldSoft,
                    activeTrackColor = StarColors.Gold,
                    inactiveTrackColor = StarColors.Glass,
                ),
            )
            GlyphIcon(Glyph.Shake, if (shake) StarColors.Gold else StarColors.TextMuted, Modifier.size(22.dp))
            Spacer(Modifier.width(6.dp))
            Switch(
                checked = shake,
                onCheckedChange = vm::setShake,
                colors = SwitchDefaults.colors(
                    checkedThumbColor = Color(0xFF241A00),
                    checkedTrackColor = StarColors.Gold,
                    uncheckedTrackColor = StarColors.Glass,
                    uncheckedBorderColor = StarColors.GlassBorder,
                ),
            )
        }
    }
}

@Composable
private fun ConnectionChip(state: StarConnection.State, name: String, onStars: () -> Unit) {
    val (label, dot) = when (state) {
        StarConnection.State.Ready -> name.ifEmpty { "Connected" } to StarColors.Green
        StarConnection.State.Scanning -> "Searching…" to StarColors.Gold
        StarConnection.State.Connecting -> "Connecting…" to StarColors.Gold
        StarConnection.State.Failed -> "Choose star" to StarColors.Red
        StarConnection.State.Idle -> "Choose star" to StarColors.TextMuted
    }
    val shape = RoundedCornerShape(50)
    Row(
        Modifier
            .clip(shape)
            .background(StarColors.Glass)
            .border(1.dp, StarColors.GlassBorder, shape)
            .clickable { onStars() }
            .padding(horizontal = 12.dp, vertical = 6.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Box(Modifier.size(8.dp).clip(CircleShape).background(dot))
        Spacer(Modifier.width(8.dp))
        Text(
            label,
            style = MaterialTheme.typography.labelMedium,
            color = StarColors.Text,
            maxLines = 1,
            softWrap = false,
            overflow = androidx.compose.ui.text.style.TextOverflow.Ellipsis,
            modifier = Modifier.widthIn(max = 140.dp),
        )
    }
}

@Composable
private fun BottomNav(page: Page, onSelect: (Page) -> Unit) {
    val shape = RoundedCornerShape(28.dp)
    Row(
        Modifier
            .navigationBarsPadding()
            .padding(horizontal = 20.dp, vertical = 10.dp)
            .fillMaxWidth()
            .clip(shape)
            .background(Color(0xCC0B1129))
            .border(1.dp, StarColors.GlassBorder, shape)
            .padding(6.dp),
        horizontalArrangement = Arrangement.SpaceEvenly,
    ) {
        Page.entries.forEach { p ->
            val on = p == page
            Column(
                Modifier
                    .weight(1f)
                    .clip(RoundedCornerShape(22.dp))
                    .background(if (on) StarColors.Gold.copy(alpha = 0.14f) else Color.Transparent)
                    .clickable { onSelect(p) }
                    .padding(vertical = 8.dp),
                horizontalAlignment = Alignment.CenterHorizontally,
            ) {
                GlyphIcon(p.glyph, if (on) StarColors.Gold else StarColors.TextMuted, Modifier.size(22.dp))
                Spacer(Modifier.height(3.dp))
                Text(
                    p.title,
                    style = MaterialTheme.typography.labelMedium,
                    color = if (on) StarColors.Gold else StarColors.TextMuted,
                )
            }
        }
    }
}
