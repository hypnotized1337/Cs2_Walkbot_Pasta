# Vesta landing page

Static GitHub Pages landing for Vesta. It intentionally has no runtime backend,
analytics, cookies or third-party JavaScript.

The landing includes downloadable, valid Vesta profiles. It has no runtime
backend and calls no external service.

## Configure links

Edit `site-config.js` before publishing:

- `releaseUrl` and `sourceUrl` may stay empty on a GitHub project page; they are
  derived from `OWNER.github.io/REPOSITORY` automatically.

## Build

Run `npm run build` in this directory. The resulting `dist` folder contains the
entire static site. The build generates the three config presets from the
current root `legit.cfg`.

Run `npm run serve` and open `http://127.0.0.1:4173/` for local preview. Do not
open `index.html` directly.

The repository workflow publishes `website/dist` to GitHub Pages after changes
to the landing page reach `main`.
