package io.github.christmasstar.protocol

/** Comet, the game on the star (protocol 4+). */
enum class GameMode(val id: String) {
    /** On the front, mirrored to the back: everyone sees the same game. */
    Single("single"),

    /** Both faces: the comet can flip through the star. */
    Double("double"),
}

/** One "game state=... mode=... ..." line from `app game`. */
data class GameStatus(
    /** off, countdown, play, paused, over or won */
    val state: String,
    val mode: GameMode,
    /** Face the comet's head is on: front or back. */
    val face: String,
    val score: Int,
    val level: Int,
    val length: Int,
    val bestSingle: Int,
    val bestDouble: Int,
) {
    val running: Boolean get() = state == "countdown" || state == "play" || state == "paused"
    val ended: Boolean get() = state == "over" || state == "won"

    fun best(mode: GameMode): Int = if (mode == GameMode.Double) bestDouble else bestSingle

    companion object {
        private const val TAG = "game state="

        fun parse(text: String): GameStatus? {
            val start = text.indexOf(TAG)
            if (start < 0) return null
            val line = text.substring(start).lineSequence().first().trim()
            val kv = line.removePrefix("game ").split(' ')
                .mapNotNull { t -> t.split('=', limit = 2).takeIf { it.size == 2 }?.let { it[0] to it[1] } }
                .toMap()
            fun int(k: String) = kv[k]?.toIntOrNull() ?: 0
            return GameStatus(
                state = kv["state"] ?: return null,
                mode = if (kv["mode"] == "double") GameMode.Double else GameMode.Single,
                face = kv["face"] ?: "front",
                score = int("score"),
                level = int("level"),
                length = int("len"),
                bestSingle = int("best1"),
                bestDouble = int("best2"),
            )
        }
    }
}
