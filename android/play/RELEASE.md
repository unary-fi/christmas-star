# Publishing the app on Google Play

## Build

From `android/` (JDK 17):

```powershell
$env:JAVA_HOME = "C:\Program Files\Microsoft\jdk-17.0.21.21-hotspot"
.\gradlew.bat bundleRelease
```

The upload file is `app/build/outputs/bundle/release/app-release.aab`, signed
with the upload key. For each new upload, raise `versionCode` (and usually
`versionName`) in `app/build.gradle.kts`.

To try the release build on a phone first:

```powershell
.\gradlew.bat assembleRelease
adb install -r app\build\outputs\apk\release\app-release.apk
```

## The upload key

`android/upload-keystore.jks` and `android/keystore.properties` (passwords)
sign the uploads. Both are ignored by git. **Back up both files somewhere
safe**, for example a password manager. With Play App Signing (the default),
Google holds the key that signs the app on users' phones; if the upload key
is ever lost, Google can reset it, but that takes a support request and time.

Without `keystore.properties` the release build is unsigned.

## Identity

- Application ID: `fi.unary.christmasstar`. It can never change after the
  first upload.
- Debug builds are `fi.unary.christmasstar.debug` and install next to the
  Play version.
- The Kotlin package stays `io.github.christmasstar`; Play does not see it.

## First upload in Play Console

1. Create the app: name "Christmas Star", type App, free.
2. Fill in the store listing from `play/listing.md`, with the graphics in
   `play/`.
3. Publish `play/privacy-policy.md` on a public web page (fill in the contact
   email first) and enter its URL.
4. Complete the App content forms (data safety, content rating, target
   audience, ads); answers are in `play/listing.md`.
5. Create a release in **Internal testing** first, upload the `.aab`, and try
   it from the Play Store on your own phone. Then promote it to Production.
6. New personal developer accounts must run a closed test with at least 12
   testers for 14 days before Production; organisation accounts don't.

## Phones that used the development build

The Play app has a new application ID, so it starts with its own pairing key.
A star paired with the old development app must be paired again: shake the
star for 5 seconds and tap it in the app.
