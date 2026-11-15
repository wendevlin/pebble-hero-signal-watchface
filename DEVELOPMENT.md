# Development

## Setup

Needs [Bun](https://bun.sh) (or `mise install`) and the Pebble SDK:

```sh
uv tool install --python 3.12 pebble-tool
pebble sdk install 4.44
bun install
```

## Commands

```sh
bun run build            # phone JS + .pbw
bun test                 # unit tests
bun run typecheck
bun run emulator:emery   # run in the emulator with logs
```

Demo mode renders a fixed scene for screenshots:
`HERO_DEMO="cond,night,disconnected,hero,city" pebble build`, e.g.
`HERO_DEMO=9,0,0,2,1` for a thunderstorm in New York with the green hero.
The codes are listed in `src/c/scene.h` and `src/c/skyline.h`.

## Layout

| Path | What |
| --- | --- |
| `src/c/main.c` | State, watch services, messages from the phone |
| `src/c/scene.c` | The comic page: panels, sky, weather, time, meters |
| `src/c/skyline.c` | The five cities |
| `src/c/comic.c` | Drawing toolkit: clipped polygons, halftone, rays, lettering |
| `src/pkjs/index.ts` | Phone side: weather, Home Assistant, settings |
| `src/pkjs/config.html` | Settings page |
| `docs/` | GitHub Pages: landing page, OAuth callback, previews |

## Good to know

- **Older watches** (basalt/chalk) have a tiny app stack and don't clip shapes
  at the screen edge. So polygon buffers are static, the big draw helpers are
  `NOINLINE`, and everything is clipped to the screen. `HERO_STACK_USAGE=1`
  prints per-function stack sizes.
- **Home Assistant login:** HA's login page dies inside the Pebble app's
  webview, which blocks web storage. So the settings ask for username and
  password, and the phone uses HA's login-flow API to get normal OAuth
  tokens. The password is never stored.
- **Don't move `docs/oauth/callback.html`.** Together with the site root, it's
  the app's OAuth identity, and its URL is built into every `.pbw`.
- **Permissions:** the manifest declares `Internet` without a domain list, plus
  `LocalNetwork` and `Location`, because Home Assistant can live anywhere.
- **Skyline previews** in `docs/previews/` are the sky panel of
  `HERO_DEMO=1,0,0,0,<city>` on emery, scaled 2×.
