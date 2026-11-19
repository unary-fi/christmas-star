// Maven Central rate-limits GitHub's shared runners (HTTP 429), so Google's
// official mirror of it is tried first; Maven Central stays as the fallback.

pluginManagement {
    repositories {
        google()
        maven("https://maven-central.storage-download.googleapis.com/maven2/") {
            name = "MavenCentralGoogleMirror"
        }
        mavenCentral()
        gradlePluginPortal()
    }
}

dependencyResolutionManagement {
    repositoriesMode.set(RepositoriesMode.FAIL_ON_PROJECT_REPOS)
    repositories {
        google()
        maven("https://maven-central.storage-download.googleapis.com/maven2/") {
            name = "MavenCentralGoogleMirror"
        }
        mavenCentral()
    }
}

rootProject.name = "ChristmasStar"
include(":app")
