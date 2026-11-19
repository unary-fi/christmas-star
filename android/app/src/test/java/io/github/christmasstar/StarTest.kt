package io.github.christmasstar

import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import io.github.christmasstar.effects.EFFECTS
import io.github.christmasstar.effects.EffectPreview
import io.github.christmasstar.star.ContentTransform
import io.github.christmasstar.star.StarGeometry
import io.github.christmasstar.star.StarLayout
import io.github.christmasstar.star.StarSampler
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import kotlin.math.abs

class StarTest {

    @Test
    fun layoutTables() {
        assertEquals(132, StarLayout.x.size)
        assertEquals(132, StarLayout.partner.size)
        for (i in 0 until 132) {
            val p = StarLayout.partner[i]
            assertEquals(i, StarLayout.partner[p])
            assertEquals(StarLayout.x[i], StarLayout.x[p], 0.01f)
            assertEquals(StarLayout.y[i], StarLayout.y[p], 0.01f)
        }
        // 49 outline segments from the PCB, all within the star's reach.
        assertEquals(49 * 4, StarLayout.edges.size)
        assertTrue(StarLayout.edges.all { abs(it) <= 126f })
    }

    @Test
    fun transformRoundTrip() {
        val t = ContentTransform(4000, 3000, 1080f, 1500f, scale = 2.5f, offset = Offset(-120f, 40f))
        val p = Offset(300f, 700f)
        val back = t.boxAt(t.sourceAt(p))
        assertEquals(p.x, back.x, 0.01f)
        assertEquals(p.y, back.y, 0.01f)
        // Unmoved: the box centre shows the source centre.
        val c = ContentTransform(4000, 3000, 1080f, 1500f).sourceAt(Offset(540f, 750f))
        assertEquals(2000f, c.x, 0.01f)
        assertEquals(1500f, c.y, 0.01f)
    }

    @Test
    fun samplerReadsUnderEachLed() {
        // Left half red, right half blue.
        val w = 200
        val h = 200
        val px = IntArray(w * h) { if (it % w < w / 2) 0xFFFF0000.toInt() else 0xFF0000FF.toInt() }
        val box = Size(1000f, 1000f)
        val t = ContentTransform(w, h, box.width, box.height)
        val colors = StarSampler.sample(px, w, h, t, StarGeometry(box))
        for (i in 0 until StarLayout.FACE_LEDS) {
            if (StarLayout.x[i] < -8f) assertEquals("LED $i", 0xFF0000, colors[i])
            if (StarLayout.x[i] > 8f) assertEquals("LED $i", 0x0000FF, colors[i])
        }
        val both = StarSampler.bothFaces(colors)
        for (i in 66 until 132) assertEquals(colors[StarLayout.partner[i]], both[i])
    }

    @Test
    fun previewsRender() {
        for (fx in EFFECTS + io.github.christmasstar.effects.EffectInfo("flash", "", "")) {
            val p = EffectPreview(fx.id)
            var lit = 0
            for (t in 0L until 6000L step 20) {
                lit += p.render(t).count { it != 0 }
            }
            assertTrue("${fx.id} lights something", lit > 0)
        }
    }
}
