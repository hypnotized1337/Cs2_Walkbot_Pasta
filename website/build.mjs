import { cp, mkdir, readFile, rm, stat, writeFile } from "node:fs/promises";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { generateConfigPresets } from "./config-presets.mjs";

const here = dirname(fileURLToPath(import.meta.url));
const root = resolve(here, "..");
const output = join(here, "dist");
const required = ["index.html", "styles.css", "app.js", "site-config.js", "robots.txt", ".nojekyll"];
const weaponGroups = ["Pistol", "SMG", "Rifle", "Shotgun", "Sniper", "Heavy"];

const isRecord = value => value !== null && typeof value === "object" && !Array.isArray(value);
const isColor = value => {
  if (!isRecord(value)) return false;
  const keys = Object.keys(value);
  return keys.includes("r") && keys.includes("g") && keys.includes("b") && keys.every(key => ["r", "g", "b", "a"].includes(key));
};

function settingNode(key, value) {
  if (isColor(value)) return { key, kind: "color" };
  if (Array.isArray(value)) {
    return { key, kind: "group", children: value.map((entry, index) => settingNode(String(index + 1), entry)) };
  }
  if (isRecord(value)) {
    return { key, kind: "group", children: Object.entries(value).map(([childKey, child]) => settingNode(childKey, child)) };
  }
  return { key, kind: typeof value };
}

function combatNodes(config, prefix) {
  const selected = Object.entries(config).filter(([key]) => prefix === "triggerbot" ? key.startsWith("triggerbot_") : !key.startsWith("triggerbot_"));
  return selected.map(([key, value]) => settingNode(key.startsWith(`${prefix}_`) ? key.slice(prefix.length + 1) : key, value));
}

function overrideNodes(config, prefix) {
  return config.map((profile, index) => {
    const fields = Object.entries(profile).filter(([key]) => key === "use_global" || key.startsWith(`${prefix}_`));
    return {
      key: weaponGroups[index] || `Profile ${index + 1}`,
      kind: "group",
      children: fields.map(([key, value]) => settingNode(key === "use_global" ? key : key.slice(prefix.length + 1), value))
    };
  });
}

function buildFeatureTree(config) {
  const roots = [
    { key: "Aimbot", kind: "group", children: [
      { key: "Global", kind: "group", children: combatNodes(config.combat_global || {}, "aimbot") },
      { key: "Weapon overrides", kind: "group", children: overrideNodes(config.combat_overrides || [], "aimbot") }
    ] },
    { key: "Triggerbot", kind: "group", children: [
      { key: "Global", kind: "group", children: combatNodes(config.combat_global || {}, "triggerbot") },
      { key: "Weapon overrides", kind: "group", children: overrideNodes(config.combat_overrides || [], "triggerbot") }
    ] },
    { key: "Visuals", kind: "group", children: Object.entries(config.esp || {}).map(([key, value]) => settingNode(key, value)) },
    { key: "Misc", kind: "group", children: Object.entries(config.misc || {}).map(([key, value]) => settingNode(key, value)) }
  ];
  return roots;
}

await rm(output, { recursive: true, force: true });
await mkdir(output, { recursive: true });
for (const file of required) await cp(join(here, file), join(output, file));
const defaultConfig = JSON.parse(await readFile(join(root, "configs", "legit.cfg"), "utf8"));
const featureTreeSource = `window.VESTA_FEATURE_TREE = ${JSON.stringify(buildFeatureTree(defaultConfig))};\n`;
await writeFile(join(here, "feature-tree.js"), featureTreeSource, "utf8");
await writeFile(join(output, "feature-tree.js"), featureTreeSource, "utf8");
await cp(join(here, "assets"), join(output, "assets"), { recursive: true });
await generateConfigPresets(root, join(output, "configs"));
await generateConfigPresets(root, join(here, "configs"));

const html = await readFile(join(output, "index.html"), "utf8");
for (const asset of ["styles.css", "app.js", "site-config.js", "feature-tree.js", "assets/vesta-interface.webp", "configs/full-legit.cfg", "configs/legit.cfg", "configs/semi-rage.cfg"]) {
  if (!html.includes(asset) && !asset.startsWith("docs/")) throw new Error(`Landing page does not reference ${asset}`);
  const info = await stat(join(output, ...asset.split("/")));
  if (info.size === 0) throw new Error(`${asset} is empty`);
}
console.log(`Vesta landing built at ${output}`);
