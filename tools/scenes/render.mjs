// SPDX-License-Identifier: AGPL-3.0-or-later
// Renders each scene of scenes.html (with the captures in artifacts/scenes/) to a transparent WebP
// in site/assets/media/. Run with PLAYWRIGHT=/path/to/playwright/index.mjs; needs ffmpeg with libwebp.
import {execFileSync} from 'node:child_process';
import {mkdirSync} from 'node:fs';
import {fileURLToPath, pathToFileURL} from 'node:url';
const {chromium} = await import(pathToFileURL(process.env.PLAYWRIGHT).href);
const here = fileURLToPath(new URL('.', import.meta.url)), root = fileURLToPath(new URL('../..', import.meta.url));
const work = root + 'artifacts/scenes/render/';
mkdirSync(work, {recursive: true});
const browser = await chromium.launch();
const page = await browser.newPage({viewport: {width: 1440, height: 900}, deviceScaleFactor: 1.5});
await page.goto(pathToFileURL(here + 'scenes.html').href);
await page.evaluate(() => Promise.all([document.fonts.ready, ...[...document.images].map(image => image.decode())]));
for (const scene of await page.locator('.scene').all()) {
  const id = await scene.getAttribute('id');
  await scene.screenshot({path: `${work}${id}.png`, omitBackground: true});
  execFileSync('ffmpeg', ['-loglevel', 'error', '-y', '-i', `${work}${id}.png`, '-c:v', 'libwebp', '-quality', '82',
    '-pix_fmt', 'yuva420p', `${root}site/assets/media/scene-${id}.webp`]);
  console.log(`site/assets/media/scene-${id}.webp`);
}
await browser.close();
