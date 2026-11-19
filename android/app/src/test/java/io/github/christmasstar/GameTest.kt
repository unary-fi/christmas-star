package io.github.christmasstar

import io.github.christmasstar.protocol.GameMode
import io.github.christmasstar.protocol.GameStatus
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class GameTest {
    @Test
    fun parsesStatusLine() {
        val s = GameStatus.parse(
            "star:~$ \r\ngame state=play mode=double face=back score=12 level=3 len=9 best1=7 best2=40\r\nstar:~$ ",
        )!!
        assertEquals("play", s.state)
        assertEquals(GameMode.Double, s.mode)
        assertEquals("back", s.face)
        assertEquals(12, s.score)
        assertEquals(3, s.level)
        assertEquals(9, s.length)
        assertEquals(7, s.best(GameMode.Single))
        assertEquals(40, s.best(GameMode.Double))
        assertTrue(s.running)
        assertFalse(s.ended)
    }

    @Test
    fun statesAndPrompts() {
        // A prompt from an earlier steering command may come first, on the same line.
        val s = GameStatus.parse("star:~$ game state=over mode=single face=front score=5 level=1 len=13 best1=5 best2=0")!!
        assertEquals(GameMode.Single, s.mode)
        assertTrue(s.ended)
        assertFalse(s.running)
        assertEquals("off", GameStatus.parse("game state=off mode=single face=front score=0 level=1 len=0 best1=0 best2=0")!!.state)
        assertNull(GameStatus.parse("unknown command"))
    }
}
