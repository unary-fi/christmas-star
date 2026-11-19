package io.github.christmasstar.media

import android.content.Context
import androidx.core.graphics.scale
import android.media.MediaMetadataRetriever
import android.net.Uri
import io.github.christmasstar.protocol.CsfxBuilder
import io.github.christmasstar.star.ContentTransform
import io.github.christmasstar.star.StarGeometry
import io.github.christmasstar.star.StarSampler
import java.io.File
import java.io.InputStream

/** Size and length of a video, with rotation applied. */
data class VideoInfo(val width: Int, val height: Int, val durationMs: Long)

object VideoExporter {
    /** Width of the frames sampled for the star (plenty for 66 LEDs). */
    const val SAMPLE_WIDTH = 192

    fun info(context: Context, uri: Uri): VideoInfo? = runCatching {
        MediaMetadataRetriever().useCompat { r ->
            r.setDataSource(context, uri)
            var w = r.extractMetadata(MediaMetadataRetriever.METADATA_KEY_VIDEO_WIDTH)!!.toInt()
            var h = r.extractMetadata(MediaMetadataRetriever.METADATA_KEY_VIDEO_HEIGHT)!!.toInt()
            val rot = r.extractMetadata(MediaMetadataRetriever.METADATA_KEY_VIDEO_ROTATION)?.toIntOrNull() ?: 0
            if (rot == 90 || rot == 270) w = h.also { h = w }
            val d = r.extractMetadata(MediaMetadataRetriever.METADATA_KEY_DURATION)?.toLongOrNull() ?: 0L
            VideoInfo(w, h, d)
        }
    }.getOrNull()

    /**
     * Turn the trimmed video into a looping .csfx effect, sampling each frame
     * exactly as the star shows it on screen (same pan and zoom).
     */
    fun export(
        context: Context,
        uri: Uri,
        name: String,
        transform: ContentTransform,
        star: StarGeometry,
        startMs: Long,
        endMs: Long,
        fps: Float,
        progress: (Float) -> Unit,
    ): Pair<InputStream, Long> {
        val frameMs = 1000f / fps
        val count = ((endMs - startMs) / frameMs).toInt().coerceAtLeast(1)
        val tmp = File(context.cacheDir, "export.frames")
        val builder = CsfxBuilder(name, loop = true, framesFile = tmp)
        val sh = (SAMPLE_WIDTH * transform.srcHeight / transform.srcWidth).coerceAtLeast(1)
        val pixels = IntArray(SAMPLE_WIDTH * sh)
        var duration = 0f
        var written = 0

        MediaMetadataRetriever().useCompat { r ->
            r.setDataSource(context, uri)
            for (k in 0 until count) {
                val tUs = ((startMs + k * frameMs) * 1000).toLong()
                val bmp = r.getScaledFrameAtTime(tUs, MediaMetadataRetriever.OPTION_CLOSEST, SAMPLE_WIDTH, sh)
                val colors = if (bmp != null) {
                    val b = if (bmp.width == SAMPLE_WIDTH && bmp.height == sh) bmp else bmp.scale(SAMPLE_WIDTH, sh)
                    b.getPixels(pixels, 0, SAMPLE_WIDTH, 0, 0, SAMPLE_WIDTH, sh)
                    StarSampler.sample(pixels, SAMPLE_WIDTH, sh, transform, star)
                } else {
                    IntArray(66)
                }
                // Keep the exact frame rate on average with whole milliseconds.
                duration += frameMs
                val d = duration.toInt()
                duration -= d
                builder.addFrame(colors, d)
                written++
                progress(written.toFloat() / count)
            }
        }
        return builder.finish()
    }

    /** MediaMetadataRetriever is AutoCloseable only from API 29. */
    private inline fun <T> MediaMetadataRetriever.useCompat(block: (MediaMetadataRetriever) -> T): T =
        try {
            block(this)
        } finally {
            release()
        }
}
