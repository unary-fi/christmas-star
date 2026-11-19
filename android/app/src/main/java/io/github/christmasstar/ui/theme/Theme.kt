package io.github.christmasstar.ui.theme

import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Typography
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.Font
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontVariation
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.sp
import io.github.christmasstar.R

/** A deep night sky, one warm gold accent and soft white text. */
object StarColors {
    val NightTop = Color(0xFF070B1E)
    val NightMid = Color(0xFF0E1534)
    val NightBottom = Color(0xFF1E1A4A)
    val Gold = Color(0xFFF2C14E)
    val GoldSoft = Color(0xFFFFE7A8)
    val Text = Color(0xFFEDEFF7)
    val TextMuted = Color(0xFF9AA3C7)
    val Glass = Color(0x14FFFFFF)
    val GlassBorder = Color(0x24FFFFFF)
    val Red = Color(0xFFE5484D)
    val Green = Color(0xFF3DD68C)
    val Blue = Color(0xFF4C8DFF)
}

@OptIn(androidx.compose.ui.text.ExperimentalTextApi::class)
private fun cormorant(weight: Int) = Font(
    R.font.cormorant_garamond,
    weight = FontWeight(weight),
    variationSettings = FontVariation.Settings(FontVariation.weight(weight)),
)

/** Elegant serif for titles. */
val DisplayFont = FontFamily(cormorant(500), cormorant(600), cormorant(700))

private val typography = Typography(
    displaySmall = TextStyle(fontFamily = DisplayFont, fontWeight = FontWeight(600), fontSize = 29.sp, letterSpacing = 0.3.sp),
    headlineSmall = TextStyle(fontFamily = DisplayFont, fontWeight = FontWeight(600), fontSize = 26.sp),
    titleLarge = TextStyle(fontFamily = DisplayFont, fontWeight = FontWeight(600), fontSize = 22.sp),
    titleMedium = TextStyle(fontFamily = DisplayFont, fontWeight = FontWeight(600), fontSize = 19.sp),
    bodyMedium = TextStyle(fontSize = 14.sp, letterSpacing = 0.2.sp),
    bodySmall = TextStyle(fontSize = 12.sp, letterSpacing = 0.3.sp),
    labelLarge = TextStyle(fontSize = 14.sp, fontWeight = FontWeight.Medium, letterSpacing = 0.4.sp),
    labelMedium = TextStyle(fontSize = 12.sp, fontWeight = FontWeight.Medium, letterSpacing = 0.6.sp),
)

private val colors = darkColorScheme(
    primary = StarColors.Gold,
    onPrimary = Color(0xFF241A00),
    secondary = StarColors.GoldSoft,
    background = StarColors.NightTop,
    surface = StarColors.NightMid,
    onSurface = StarColors.Text,
    onBackground = StarColors.Text,
    onSurfaceVariant = StarColors.TextMuted,
    surfaceVariant = Color(0xFF1A2147),
    outline = StarColors.GlassBorder,
)

@Composable
fun ChristmasStarTheme(content: @Composable () -> Unit) {
    MaterialTheme(colorScheme = colors, typography = typography, content = content)
}
