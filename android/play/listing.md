# Google Play store listing – Christmas Star

Texts to paste into Play Console. Character limits in brackets.

## App name (30)

Christmas Star

## Short description (80)

Light up your Christmas Star: colours, photos, videos and festive effects.

## Full description (4000)

Christmas Star brings your smart Christmas star to life. Connect over
Bluetooth and fill both faces of the star with light.

★ Festive effects
Sixteen effects run on the star itself and keep going when your phone is
away: Golden Star, Liquid Gold, Snowfall, Shooting Stars, Candlelight,
Hearth & Frost, Northern Lights, Candy Cane, Tree Lights, Star of Bethlehem,
Fireworks, Christmas Disco, Frost, Twinkle, Comets and Rainbow. Each one has a live preview,
and you can set the speed. New effects arrive with the star's firmware
updates, with no app update needed.

★ Tilt and shake
Liquid Gold pools at the lowest point and flows when you turn the star.
Shake the star to knock its lights loose: they fall in their own colours and
then glow back, with any effect.

★ Your own colours
Mix any colour with three sliders, for the front, the back or both faces,
or start from festive presets.

★ Photos and videos
Choose a photo or video, move and zoom it behind the star's outline, and the
star shows the colours under its lights, live. Save a video loop on the star
and it keeps playing without the phone.

★ One star, one phone
Each star pairs with one phone, so several stars and phones can share a
room. Name your star, and hand it over to another phone when you like.

The app needs a Christmas Star with Bluetooth. It works without an internet
connection and collects no personal data.

## Category

Lifestyle (alternative: House & Home)

## Tags / keywords to consider

Christmas, star, Christmas lights, LED, decoration, Bluetooth

## Release notes for 1.0.0 (500)

The first release of Christmas Star:
• 16 festive effects with live previews
• Colours, photos and videos on the star
• Tilt and shake effects
• Pair your phone with your star and give it a name

## Contact details

- Email: info@unary.fi
- Website: https://christmas-star.unary.fi
- Privacy policy URL: https://christmas-star.unary.fi/privacy-policy

## Graphics

| Item | File | Notes |
|---|---|---|
| App icon | `icon-512.png` | 512 × 512, Play applies the rounded mask |
| Feature graphic | `feature-graphic.png` | 1024 × 500 |
| Phone screenshots | `screenshots/*.png` | 2–8 images, 1440 × 2880 (2:1) |

Regenerate the icon and feature graphic with `python android/play/make_assets.py`.

## App content answers

**Data safety**
- Does the app collect or share user data? **No.**
- Is all data encrypted in transit? Not applicable (no data leaves the device
  except colours sent to the star over Bluetooth).
- Can users request deletion? Not applicable (nothing is collected). The app's
  data is removed by uninstalling it or clearing its storage.

**Content rating questionnaire:** a utility/lifestyle app with no violence,
no user-generated content shared with others, no chat, no location sharing,
no purchases and no gambling. Expected rating: Everyone / PEGI 3.

**Target audience:** 13 and over is the simplest choice. If you pick ages
under 13, the Families policy applies (it can be met: no ads, no data).

**Ads:** No ads. **In-app purchases:** none. **Government app:** no.
**Financial features:** none. **Health:** none.

**Permissions** (no special declaration form needed):
- Nearby devices (BLUETOOTH_SCAN with neverForLocation, BLUETOOTH_CONNECT):
  find and control the star.
- Location (Android 9–11 only, maxSdkVersion 30): required by Android for
  Bluetooth scanning on those versions; the location itself is never used.
