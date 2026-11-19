package io.github.christmasstar.ui

import android.graphics.Bitmap
import android.graphics.ImageDecoder
import android.net.Uri
import android.provider.OpenableColumns
import android.view.TextureView
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.PickVisualMediaRequest
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.detectTransformGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.RangeSlider
import androidx.compose.material3.Slider
import androidx.compose.material3.SliderDefaults
import androidx.compose.material3.Switch
import androidx.compose.material3.SwitchDefaults
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableLongStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.clipToBounds
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.layout.onSizeChanged
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.unit.dp
import androidx.compose.ui.viewinterop.AndroidView
import androidx.core.graphics.scale
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.media3.common.MediaItem
import androidx.media3.common.Player
import androidx.media3.exoplayer.ExoPlayer
import io.github.christmasstar.ble.StarConnection
import io.github.christmasstar.media.VideoExporter
import io.github.christmasstar.media.VideoInfo
import io.github.christmasstar.protocol.CsfxBuilder
import io.github.christmasstar.star.ContentTransform
import io.github.christmasstar.star.StarCanvas
import io.github.christmasstar.star.StarGeometry
import io.github.christmasstar.star.StarSampler
import io.github.christmasstar.ui.theme.StarColors
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.withContext
import kotlin.math.max
import kotlin.math.roundToInt

private sealed interface Media {
    val width: Int
    val height: Int

    /** A photo: the shown bitmap and a small copy for sampling. */
    class Photo(val bitmap: Bitmap, val small: IntArray, val smallW: Int, val smallH: Int) : Media {
        override val width = bitmap.width
        override val height = bitmap.height
    }

    class Video(val uri: Uri, val info: VideoInfo, val name: String) : Media {
        override val width = info.width
        override val height = info.height
    }
}

private val FPS_OPTIONS = listOf(12.5f to "12.5", 25f to "25", 50f to "50")

@Composable
fun PictureScreen(vm: StarViewModel) {
    val context = LocalContext.current
    val density = LocalDensity.current
    val connState by vm.connection.state.collectAsStateWithLifecycle()
    val star by vm.star.collectAsStateWithLifecycle()
    val upload by vm.upload.collectAsStateWithLifecycle()
    val liveFps by vm.liveFps.collectAsStateWithLifecycle()

    var media by remember { mutableStateOf<Media?>(null) }
    var loadError by remember { mutableStateOf<String?>(null) }
    var box by remember { mutableStateOf(Size.Zero) }
    var scale by remember { mutableFloatStateOf(1f) }
    var offset by remember { mutableStateOf(Offset.Zero) }
    var colors by remember { mutableStateOf<IntArray?>(null) }
    var live by remember { mutableStateOf(true) }

    // Video playback state.
    var textureView by remember { mutableStateOf<TextureView?>(null) }
    var playing by remember { mutableStateOf(true) }
    var position by remember { mutableLongStateOf(0L) }
    var trim by remember { mutableStateOf(0f..1f) }
    var fps by remember { mutableFloatStateOf(25f) }

    val player = remember {
        ExoPlayer.Builder(context).build().apply { repeatMode = Player.REPEAT_MODE_ALL }
    }
    DisposableEffect(Unit) {
        onDispose {
            player.release()
            // Leave the last picture on the star.
            vm.setLive(null)
        }
    }

    val picker = rememberLauncherForActivityResult(ActivityResultContracts.PickVisualMedia()) { uri ->
        if (uri != null) {
            media = null
            loadError = null
            scale = 1f
            offset = Offset.Zero
            trim = 0f..1f
            val type = context.contentResolver.getType(uri) ?: ""
            if (type.startsWith("video")) {
                val info = VideoExporter.info(context, uri)
                if (info == null) {
                    loadError = "This video can't be read."
                } else {
                    media = Media.Video(uri, info, displayName(context, uri) ?: "video")
                    player.setMediaItem(MediaItem.fromUri(uri))
                    player.prepare()
                    player.play()
                    playing = true
                }
            } else {
                player.stop()
                media = runCatching { loadPhoto(context, uri) }.getOrElse {
                    loadError = "This picture can't be read."
                    null
                }
            }
        }
    }

    val transform = media?.let {
        if (box.width > 0) ContentTransform(it.width, it.height, box.width, box.height, scale, offset) else null
    }
    val currentTransform by rememberUpdatedState(transform)
    val liveOn by rememberUpdatedState(live && connState == StarConnection.State.Ready)

    // Photo: resample whenever it moves.
    LaunchedEffect(media, transform) {
        val m = media as? Media.Photo ?: return@LaunchedEffect
        val t = transform ?: return@LaunchedEffect
        colors = StarSampler.sample(m.small, m.smallW, m.smallH, t, StarGeometry(box))
    }
    // Video: sample the playing frames ~25 times a second.
    LaunchedEffect(media) {
        val m = media as? Media.Video ?: return@LaunchedEffect
        val w = VideoExporter.SAMPLE_WIDTH
        val h = max(1, w * m.height / m.width)
        val px = IntArray(w * h)
        while (true) {
            delay(40)
            position = player.currentPosition
            val tv = textureView ?: continue
            val t = currentTransform ?: continue
            val bmp = tv.getBitmap(w, h) ?: continue
            bmp.getPixels(px, 0, w, 0, 0, w, h)
            bmp.recycle()
            colors = StarSampler.sample(px, w, h, t, StarGeometry(box))
        }
    }
    // Stream to the star while live.
    LaunchedEffect(colors, liveOn) {
        val c = colors
        if (liveOn && c != null) vm.setLive(c) else if (!liveOn) vm.setLive(null)
    }

    Column(Modifier.fillMaxSize()) {
        Box(
            Modifier
                .weight(1f)
                .fillMaxWidth()
                .padding(horizontal = 16.dp)
                .clip(RoundedCornerShape(24.dp))
                .background(Color(0x33000000))
                .clipToBounds()
                .onSizeChanged { box = Size(it.width.toFloat(), it.height.toFloat()) }
                .pointerInput(media) {
                    if (media != null) {
                        detectTransformGestures { centroid, pan, zoom, _ ->
                            val c = Offset(size.width / 2f, size.height / 2f)
                            val newScale = (scale * zoom).coerceIn(0.2f, 12f)
                            val z = newScale / scale
                            offset = offset * z + (centroid - c) * (1f - z) + pan
                            scale = newScale
                        }
                    }
                },
        ) {
            val m = media
            if (m != null && box.width > 0) {
                val fit = minOf(box.width / m.width, box.height / m.height)
                val wDp = with(density) { (m.width * fit).toDp() }
                val hDp = with(density) { (m.height * fit).toDp() }
                val layer = Modifier
                    .size(wDp, hDp)
                    .align(Alignment.Center)
                    .graphicsLayer {
                        scaleX = scale
                        scaleY = scale
                        translationX = offset.x
                        translationY = offset.y
                    }
                when (m) {
                    is Media.Photo -> Image(m.bitmap.asImageBitmap(), null, layer, contentScale = ContentScale.FillBounds)
                    is Media.Video -> AndroidView(
                        factory = { ctx ->
                            TextureView(ctx).also {
                                textureView = it
                                player.setVideoTextureView(it)
                            }
                        },
                        modifier = layer,
                    )
                }
                // Dim the picture slightly so the star reads well on top.
                Box(Modifier.fillMaxSize().background(Color(0x40070B1E)))
            }
            StarCanvas(colors, Modifier.fillMaxSize(), outlineAlpha = 0.9f)
            if (m == null) {
                Column(
                    Modifier.align(Alignment.Center).padding(24.dp),
                    horizontalAlignment = Alignment.CenterHorizontally,
                ) {
                    Text(
                        "Light the star with a picture",
                        style = MaterialTheme.typography.headlineSmall,
                        color = StarColors.Text,
                    )
                    Spacer(Modifier.height(6.dp))
                    Caption("Choose a photo or video, then move and zoom it behind the star.")
                    loadError?.let {
                        Spacer(Modifier.height(6.dp))
                        Text(it, color = StarColors.Red, style = MaterialTheme.typography.bodySmall)
                    }
                    Spacer(Modifier.height(16.dp))
                    GoldButton("Choose photo or video") {
                        picker.launch(PickVisualMediaRequest(ActivityResultContracts.PickVisualMedia.ImageAndVideo))
                    }
                }
            }
        }

        // Controls.
        GlassCard(Modifier.padding(horizontal = 16.dp, vertical = 10.dp).fillMaxWidth()) {
            Column(Modifier.padding(14.dp)) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    OutlinedButton(
                        onClick = {
                            picker.launch(PickVisualMediaRequest(ActivityResultContracts.PickVisualMedia.ImageAndVideo))
                        },
                    ) { Text("Choose", color = StarColors.Text) }
                    Spacer(Modifier.weight(1f))
                    Column(horizontalAlignment = Alignment.End) {
                        Text("Live on the star", style = MaterialTheme.typography.labelLarge, color = StarColors.Text)
                        Caption(
                            when {
                                connState != StarConnection.State.Ready -> "not connected"
                                live && liveFps > 0f -> "${liveFps.roundToInt()} frames/s"
                                live -> "starting…"
                                else -> "paused"
                            },
                        )
                    }
                    Spacer(Modifier.width(10.dp))
                    Switch(
                        checked = live,
                        onCheckedChange = { live = it },
                        colors = SwitchDefaults.colors(
                            checkedThumbColor = Color(0xFF241A00),
                            checkedTrackColor = StarColors.Gold,
                            uncheckedTrackColor = StarColors.Glass,
                            uncheckedBorderColor = StarColors.GlassBorder,
                        ),
                    )
                }

                when (val m = media) {
                    is Media.Photo -> {
                        Spacer(Modifier.height(8.dp))
                        Caption("The star keeps the picture when you leave this page, also after a restart.")
                    }
                    is Media.Video -> VideoControls(
                        vm = vm,
                        video = m,
                        playing = playing,
                        position = position,
                        trim = trim,
                        fps = fps,
                        capacity = star?.capacityFrames ?: 0,
                        uploadRunning = upload.running,
                        connected = connState == StarConnection.State.Ready,
                        onPlayPause = {
                            playing = !playing
                            if (playing) player.play() else player.pause()
                        },
                        onSeek = { player.seekTo(it) },
                        onTrim = { trim = it },
                        onFps = { fps = it },
                        onSave = {
                            val t = transform ?: return@VideoControls
                            val geometry = StarGeometry(box)
                            val start = (trim.start * m.info.durationMs).toLong()
                            val end = (trim.endInclusive * m.info.durationMs).toLong()
                            live = false
                            vm.uploadEffect { progress ->
                                withContext(Dispatchers.Default) {
                                    VideoExporter.export(context, m.uri, m.name.take(16), t, geometry, start, end, fps, progress)
                                }
                            }
                        },
                    )
                    null -> Unit
                }
            }
        }
    }

    if (upload.running || upload.done || upload.error != null) {
        UploadDialog(upload, onCancel = vm::cancelUpload, onClose = vm::dismissUpload)
    }
}

@Composable
private fun VideoControls(
    vm: StarViewModel,
    video: Media.Video,
    playing: Boolean,
    position: Long,
    trim: ClosedFloatingPointRange<Float>,
    fps: Float,
    capacity: Long,
    uploadRunning: Boolean,
    connected: Boolean,
    onPlayPause: () -> Unit,
    onSeek: (Long) -> Unit,
    onTrim: (ClosedFloatingPointRange<Float>) -> Unit,
    onFps: (Float) -> Unit,
    onSave: () -> Unit,
) {
    val duration = video.info.durationMs.coerceAtLeast(1)
    val sliderColors = SliderDefaults.colors(
        thumbColor = StarColors.GoldSoft,
        activeTrackColor = StarColors.Gold,
        inactiveTrackColor = StarColors.Glass,
    )
    Spacer(Modifier.height(6.dp))
    Row(verticalAlignment = Alignment.CenterVertically) {
        Box(
            Modifier.size(40.dp).clip(CircleShape).background(StarColors.Glass).clickable(onClick = onPlayPause),
            contentAlignment = Alignment.Center,
        ) {
            GlyphIcon(if (playing) Glyph.Pause else Glyph.Play, StarColors.Gold, Modifier.size(20.dp))
        }
        Slider(
            value = position.toFloat() / duration,
            onValueChange = { onSeek((it * duration).toLong()) },
            modifier = Modifier.weight(1f).padding(start = 10.dp),
            colors = sliderColors,
        )
    }
    Text("Loop to save", style = MaterialTheme.typography.labelLarge, color = StarColors.Text)
    RangeSlider(value = trim, onValueChange = onTrim, colors = sliderColors)

    val seconds = (trim.endInclusive - trim.start) * duration / 1000f
    val frames = (seconds * fps).toLong().coerceAtLeast(1)
    val bytes = CsfxBuilder.HEADER_SIZE + frames * CsfxBuilder.FRAME_SIZE
    val uploadSecs = bytes / vm.uploadRate
    val fits = capacity == 0L || frames <= capacity

    Row(verticalAlignment = Alignment.CenterVertically) {
        Segmented(FPS_OPTIONS, fps, onFps)
        Spacer(Modifier.width(10.dp))
        Column(Modifier.weight(1f)) {
            Caption("frames/s · %.1f s".format(seconds))
            Caption(if (fits) "about ${formatDuration(uploadSecs)} to send" else "too long for the star's memory")
        }
    }
    Spacer(Modifier.height(8.dp))
    GoldButton(
        "Save loop on the star",
        enabled = fits && connected && !uploadRunning,
        modifier = Modifier.fillMaxWidth(),
        onClick = onSave,
    )
}

@Composable
private fun UploadDialog(state: UploadState, onCancel: () -> Unit, onClose: () -> Unit) {
    Box(
        Modifier.fillMaxSize().background(Color(0xAA05081A)).clickable(enabled = false) {},
        contentAlignment = Alignment.Center,
    ) {
        GlassCard(Modifier.padding(32.dp).fillMaxWidth()) {
            Column(Modifier.padding(22.dp), horizontalAlignment = Alignment.CenterHorizontally) {
                GlyphIcon(Glyph.Upload, StarColors.Gold, Modifier.size(32.dp))
                Spacer(Modifier.height(10.dp))
                Text(
                    when {
                        state.done -> "Saved on the star"
                        state.error != null -> "Not saved"
                        else -> state.stage
                    },
                    style = MaterialTheme.typography.titleLarge,
                    color = StarColors.Text,
                )
                Spacer(Modifier.height(12.dp))
                if (state.running) {
                    LinearProgressIndicator(
                        progress = { state.progress },
                        modifier = Modifier.fillMaxWidth(),
                        color = StarColors.Gold,
                        trackColor = StarColors.Glass,
                    )
                    Spacer(Modifier.height(6.dp))
                    Caption("${(state.progress * 100).roundToInt()} %")
                    Spacer(Modifier.height(12.dp))
                    OutlinedButton(onClick = onCancel) { Text("Cancel", color = StarColors.Text) }
                } else {
                    Caption(state.error ?: "It keeps playing even when the app is closed.")
                    Spacer(Modifier.height(12.dp))
                    GoldButton("Close", onClick = onClose)
                }
            }
        }
    }
}

@Composable
fun GoldButton(text: String, modifier: Modifier = Modifier, enabled: Boolean = true, onClick: () -> Unit) {
    Button(
        onClick = onClick,
        enabled = enabled,
        modifier = modifier,
        colors = ButtonDefaults.buttonColors(
            containerColor = StarColors.Gold,
            contentColor = Color(0xFF241A00),
            disabledContainerColor = StarColors.Glass,
            disabledContentColor = StarColors.TextMuted,
        ),
    ) { Text(text, style = MaterialTheme.typography.labelLarge) }
}

private fun formatDuration(s: Float): String = when {
    s < 60 -> "${s.roundToInt().coerceAtLeast(1)} s"
    s < 3600 -> "${(s / 60).roundToInt()} min"
    else -> "%.1f h".format(s / 3600)
}

private fun loadPhoto(context: android.content.Context, uri: Uri): Media.Photo {
    val src = ImageDecoder.createSource(context.contentResolver, uri)
    val bmp = ImageDecoder.decodeBitmap(src) { decoder, info, _ ->
        decoder.allocator = ImageDecoder.ALLOCATOR_SOFTWARE
        val longest = max(info.size.width, info.size.height)
        if (longest > 1600) {
            val k = 1600f / longest
            decoder.setTargetSize((info.size.width * k).toInt(), (info.size.height * k).toInt())
        }
    }
    val sw = minOf(400, bmp.width)
    val sh = max(1, sw * bmp.height / bmp.width)
    val small = bmp.scale(sw, sh)
    val px = IntArray(sw * sh)
    small.getPixels(px, 0, sw, 0, 0, sw, sh)
    return Media.Photo(bmp, px, sw, sh)
}

private fun displayName(context: android.content.Context, uri: Uri): String? =
    context.contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)?.use { c ->
        if (c.moveToFirst()) c.getString(0)?.substringBeforeLast('.') else null
    }
