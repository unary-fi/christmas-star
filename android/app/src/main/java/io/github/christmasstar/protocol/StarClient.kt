package io.github.christmasstar.protocol

import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.selects.select
import java.io.InputStream

/** The byte link to the star (BLE Transparent UART). */
interface StarLink {
    val input: ByteQueue
    suspend fun write(data: ByteArray)
}

/** One-line state reported by `app state`. */
data class StarState(
    val effect: String,
    val brightness: Int,
    val shake: Boolean,
    val speed: Int,
    val fps: Int,
    val capacityFrames: Long,
    val flashName: String?,
    val flashFrames: Int,
    val owner: String = "free",
    val name: String = "",
) {
    companion object {
        fun parse(text: String): StarState? {
            val full = text.lineSequence().firstOrNull { it.startsWith("state ") } ?: return null
            // name= is last and may hold spaces (firmware protocol 2).
            val nameAt = full.indexOf(" name=")
            val name = if (nameAt >= 0) full.substring(nameAt + 6).trim() else ""
            val line = if (nameAt >= 0) full.substring(0, nameAt) else full
            val flashAt = line.indexOf(" flash=")
            val framesAt = line.indexOf(" frames=")
            val kv = line.substring(6, if (flashAt > 0) flashAt else line.length)
                .split(' ').mapNotNull { p -> p.split('=', limit = 2).takeIf { it.size == 2 } }
                .associate { it[0] to it[1] }
            val flash = if (flashAt > 0 && framesAt > flashAt) line.substring(flashAt + 7, framesAt) else "-"
            val afterFrames = if (framesAt > 0) line.substring(framesAt + 8).trim().split(' ') else emptyList()
            val frames = afterFrames.firstOrNull()?.toIntOrNull() ?: 0
            val tail = afterFrames.drop(1).mapNotNull { p -> p.split('=', limit = 2).takeIf { it.size == 2 } }
                .associate { it[0] to it[1] }
            return StarState(
                effect = kv["effect"] ?: return null,
                brightness = kv["bright"]?.toIntOrNull() ?: 128,
                shake = kv["shake"] == "1",
                speed = kv["speed"]?.toIntOrNull() ?: 100,
                fps = kv["fps"]?.toIntOrNull() ?: 0,
                capacityFrames = kv["cap"]?.toLongOrNull() ?: 0,
                flashName = flash.takeIf { it != "-" && frames > 0 },
                flashFrames = frames,
                owner = tail["owner"] ?: "free",
                name = name,
            )
        }
    }
}

/**
 * Serialises everything sent to the star through one loop: shell commands,
 * the binary live stream and XMODEM uploads. When a command arrives while
 * streaming, the stream is ended, the command runs, and streaming resumes.
 */
class StarClient(private val link: StarLink) {

    private sealed interface Req {
        class Command(val text: String, val reply: CompletableDeferred<String>?) : Req
        class Upload(
            val stream: InputStream,
            val size: Long,
            val progress: (Long) -> Unit,
            val cancelled: () -> Boolean,
            val done: CompletableDeferred<Unit>,
            val command: String = "effect upload",
        ) : Req
        class Raw(val text: String, val until: String, val timeoutMs: Long, val reply: CompletableDeferred<ByteArray>) : Req
        class Login(
            val keys: List<ByteArray>,
            val pairKey: ByteArray?,
            val done: CompletableDeferred<LoginResult>,
        ) : Req
    }

    private val requests = Channel<Req>(Channel.UNLIMITED)
    private val wake = Channel<Unit>(Channel.CONFLATED)
    private val liveFrame = MutableStateFlow<ByteArray?>(null)
    @Volatile private var liveWanted = false
    private var streaming = false

    private val _liveFps = MutableStateFlow(0f)
    /** Frames per second acknowledged by the star while streaming. */
    val liveFps: StateFlow<Float> = _liveFps.asStateFlow()

    fun start(scope: CoroutineScope): Job = scope.launch(Dispatchers.IO) {
        while (isActive) {
            try {
                step()
            } catch (e: Exception) {
                if (e is kotlinx.coroutines.CancellationException) throw e
                streaming = false
                delay(200)
            }
        }
    }

    /** Run a command and return its output (without the prompt). */
    suspend fun command(text: String): String {
        val reply = CompletableDeferred<String>()
        requests.send(Req.Command(text, reply))
        wake.trySend(Unit)
        return reply.await()
    }

    /** Send a command without waiting for its output. */
    fun send(text: String) {
        requests.trySend(Req.Command(text, null))
        wake.trySend(Unit)
    }

    /** Stream these 66 front colours (back copies front); null stops streaming. */
    fun setLive(front: IntArray?) {
        if (front == null) {
            liveWanted = false
        } else {
            liveFrame.value = StreamFrame.encodeFront(front)
            liveWanted = true
        }
        wake.trySend(Unit)
    }

    /**
     * Unlock the star's shell: try [keys] in order; if none works and [pairKey]
     * is given while the star's pairing window is open, pair with it.
     */
    suspend fun login(keys: List<ByteArray>, pairKey: ByteArray? = null): LoginResult {
        val done = CompletableDeferred<LoginResult>()
        requests.send(Req.Login(keys, pairKey, done))
        wake.trySend(Unit)
        return done.await()
    }

    /**
     * Run a command whose reply holds binary data: everything up to [until]
     * and the following prompt, as raw bytes.
     */
    suspend fun raw(text: String, until: String, timeoutMs: Long = 30_000): ByteArray {
        val reply = CompletableDeferred<ByteArray>()
        requests.send(Req.Raw(text, until, timeoutMs, reply))
        wake.trySend(Unit)
        return reply.await()
    }

    /** Upload a .csfx effect into the star's flash (replaces the stored one). */
    suspend fun upload(
        stream: InputStream,
        size: Long,
        progress: (Long) -> Unit,
        cancelled: () -> Boolean,
        command: String = "effect upload",
    ) {
        val done = CompletableDeferred<Unit>()
        requests.send(Req.Upload(stream, size, progress, cancelled, done, command))
        wake.trySend(Unit)
        done.await()
    }

    /** Forget any queued work (e.g. after the link dropped). */
    fun reset() {
        while (true) {
            when (val r = requests.tryReceive().getOrNull() ?: break) {
                is Req.Command -> r.reply?.completeExceptionally(IllegalStateException("disconnected"))
                is Req.Upload -> r.done.completeExceptionally(IllegalStateException("disconnected"))
                is Req.Login -> r.done.completeExceptionally(IllegalStateException("disconnected"))
                is Req.Raw -> r.reply.completeExceptionally(IllegalStateException("disconnected"))
            }
        }
        streaming = false
        liveWanted = false
    }

    private suspend fun step() {
        val req = requests.tryReceive().getOrNull()
        if (req != null) {
            if (streaming) exitStream()
            handle(req)
            return
        }
        if (liveWanted) {
            if (!streaming) enterStream()
            pump()
        } else {
            if (streaming) exitStream()
            select<Unit> {
                requests.onReceive { handle(it) }
                wake.onReceive { }
            }
        }
    }

    private suspend fun handle(req: Req) {
        when (req) {
            is Req.Command -> try {
                link.input.clear()
                link.write((req.text + "\r\n").toByteArray())
                if (req.reply != null) {
                    val out = link.input.readUntil(PROMPT, 4000)
                    req.reply.complete(clean(out, req.text))
                }
            } catch (e: Exception) {
                req.reply?.completeExceptionally(e)
                throw e
            }
            is Req.Raw -> try {
                link.input.clear()
                link.write((req.text + "\r\n").toByteArray())
                val out = link.input.readUntil(req.until, req.timeoutMs) + link.input.readUntil(PROMPT, 2000)
                req.reply.complete(out.toByteArray(Charsets.ISO_8859_1))
            } catch (e: Exception) {
                req.reply.completeExceptionally(e)
                throw e
            }
            is Req.Login -> try {
                req.done.complete(doLogin(req))
            } catch (e: Exception) {
                req.done.completeExceptionally(e)
                throw e
            }
            is Req.Upload -> try {
                doUpload(req)
                req.done.complete(Unit)
            } catch (e: Exception) {
                req.done.completeExceptionally(e)
                if (e !is XmodemException) throw e
            }
        }
    }

    private fun clean(out: String, cmd: String): String =
        out.replace("\r", "").lines()
            .map { it.removePrefix(PROMPT).trim() }
            .filter { it.isNotEmpty() && it != cmd }
            .joinToString("\n")

    /** One reply line of the locked shell (the prompt may follow after unlocking). */
    private suspend fun readLine(timeoutMs: Long = 3000): String {
        val end = System.currentTimeMillis() + timeoutMs
        var text = ""
        while (System.currentTimeMillis() < end) {
            text += link.input.readUntil("\n", end - System.currentTimeMillis())
            val line = text.replace("\r", "").lines().map { it.trim() }
                .firstOrNull { it.isNotEmpty() && !it.startsWith(PROMPT.trim()) }
            if (line != null) return line
        }
        return ""
    }

    private suspend fun hello(): Hello {
        link.input.clear()
        link.write("app hello\r\n".toByteArray())
        repeat(3) {
            Hello.parse(readLine())?.let { return it }
        }
        throw IllegalStateException("The star does not answer")
    }

    private suspend fun doLogin(req: Req.Login): LoginResult {
        var h = hello()
        if (h.proto < 2) return LoginResult(h, "open")
        if (h.access != "none") return LoginResult(h, h.access)
        req.keys.forEachIndexed { i, key ->
            if (i > 0) h = hello()
            link.write("app auth ${Pairing.authMac(key, h.nonce)}\r\n".toByteArray())
            val reply = readLine()
            if (reply.startsWith("ok access=")) return LoginResult(h, reply.substringAfter("="))
        }
        val pk = req.pairKey
        if (pk != null && h.pairingOpen) {
            link.write("app pair ${Pairing.hex(pk)}\r\n".toByteArray())
            if (readLine().startsWith("ok paired")) {
                return LoginResult(h.copy(owner = "paired", pairingOpen = false), "owner")
            }
        }
        return LoginResult(h, "none")
    }

    private suspend fun doUpload(req: Req.Upload) {
        link.input.clear()
        link.write((req.command + "\r\n").toByteArray())
        val intro = link.input.readUntil("now...", 6000)
        if (!intro.contains("XMODEM")) {
            throw XmodemException(clean(intro, req.command).ifEmpty { "the star did not accept the upload" })
        }
        val port = object : BytePort {
            override suspend fun write(data: ByteArray) = link.write(data)
            override suspend fun read(timeoutMs: Long) = link.input.read(timeoutMs)
        }
        XmodemSender(port).send(req.stream, req.size, req.progress, req.cancelled)
        val result = link.input.readUntil(PROMPT, 10_000)
        if (!result.contains("upload OK")) {
            throw XmodemException(clean(result, "").ifEmpty { "upload failed" })
        }
    }

    private suspend fun enterStream() {
        link.input.clear()
        link.write("stream\r\n".toByteArray())
        val deadline = System.currentTimeMillis() + 2000
        while (System.currentTimeMillis() < deadline) {
            if (link.input.read(deadline - System.currentTimeMillis()) == StreamFrame.ACK) {
                streaming = true
                return
            }
        }
        throw IllegalStateException("the star did not enter stream mode")
    }

    private suspend fun exitStream() {
        streaming = false
        link.write(StreamFrame.end())
        val deadline = System.currentTimeMillis() + 800
        while (System.currentTimeMillis() < deadline) {
            if (link.input.read(deadline - System.currentTimeMillis()) == StreamFrame.ACK) break
        }
        delay(100)
        link.input.clear()
        _liveFps.value = 0f
    }

    /** Send frames (latest wins, two in flight) until a command arrives or live stops. */
    @OptIn(kotlinx.coroutines.ExperimentalCoroutinesApi::class)
    private suspend fun pump() {
        var inflight = 0
        var lastSent: ByteArray? = null
        var lastSendAt = 0L
        var acks = 0
        var windowStart = System.currentTimeMillis()

        while (liveWanted && requests.isEmpty) {
            val now = System.currentTimeMillis()
            val frame = liveFrame.value
            if (frame != null && frame !== lastSent && inflight < WINDOW) {
                link.write(frame)
                lastSent = frame
                lastSendAt = now
                inflight++
            } else if (lastSent != null && inflight == 0 && now - lastSendAt > KEEPALIVE_MS) {
                // The star leaves stream mode after 2 s of silence.
                link.write(lastSent)
                lastSendAt = now
                inflight++
            }
            when (link.input.read(15)) {
                StreamFrame.ACK -> { inflight = maxOf(0, inflight - 1); acks++ }
                StreamFrame.NAK -> inflight = maxOf(0, inflight - 1)
            }
            if (inflight > 0 && now - lastSendAt > ACK_TIMEOUT_MS) inflight = 0
            if (now - windowStart >= 1000) {
                _liveFps.value = acks * 1000f / (now - windowStart)
                acks = 0
                windowStart = now
            }
        }
    }

    companion object {
        const val PROMPT = "star:~$ "
        private const val WINDOW = 2
        private const val KEEPALIVE_MS = 1000L
        private const val ACK_TIMEOUT_MS = 700L
    }
}
