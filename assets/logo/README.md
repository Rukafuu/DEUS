# DEUS logo assets

This directory contains the vector reconstruction of the DEUS identity.

- `deus-logo.svg` is the canonical, static production logo.
- `deus-logo-animated.svg` adds a restrained entrance, eye focus, and traveling
  pipeline highlight using self-contained CSS.
- `deus-logo-preview.html` demonstrates the animated asset and two smaller sizes.

Both SVGs are genuine vector artwork. They contain no embedded raster image or
external dependency. The reconstruction follows the symbol-only source in `deus-reference.png` and
simplifies a few overlaps so the silhouette remains clean at navbar size and
each ribbon can be animated independently. It does not invent a wordmark that
is absent from the source. Logical groups include `deus-symbol`, `ribbon-left`,
`ribbon-right`, `eye`, and `eye-core`.

## Embed

For normal content, use the static asset as an image:

```html
<img src="/assets/logo/deus-logo.svg" width="480"
     alt="DEUS — Decoupled Extended Unitary Script">
```

Use the animated file in a hero in the same way. Its animation is internal, so
no site JavaScript or external stylesheet is required:

```html
<img src="/assets/logo/deus-logo-animated.svg"
     alt="DEUS — Decoupled Extended Unitary Script">
```

Control size with CSS (`width`, `max-width`, and `height: auto`). The `viewBox`
makes both assets scale without loss. For inline SVG, preserve the existing
`title`, `desc`, and their `aria-labelledby` relationship, or replace them with
context-specific accessible text. Decorative instances should use an empty alt
on `<img>`.

The animated file includes `prefers-reduced-motion: reduce`; all entrance, eye,
and flow animations stop when the user requests reduced motion. The static file
is the explicit no-motion variant.

The square symbol works on light and dark backgrounds. At favicon sizes, the
eye and broad ribbon silhouette remain the essential recognition features.
