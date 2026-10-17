// 2D ВІЙНА на карті (TypeScript / HTML canvas)
// Ви граєте за СИНІХ (ліворуч), ШІ керує ЧЕРВОНИМИ (праворуч).
export {};

const W = 1280;
const H = 720;
const INCOME = 14;
const HQ_RANGE = 230;

type Side = "blue" | "red";
type Dom = "ground" | "sea" | "air";
type UnitKind = "tank" | "plane" | "ship" | "shahed";
type StrikeKind = "shahed" | "missile" | "kab" | "nuke";

interface UnitType {
  name: string; cost: number; hp: number; speed: number; range: number;
  dmg: number; cd: number; dom: Dom; sight: number; hits: Dom[];
}
interface Unit {
  kind: UnitKind; side: Side; x: number; y: number; hp: number; cd: number;
  angle: number; tx: number; ty: number;
}
interface Hq { x: number; y: number; hp: number; max: number; cd: number }
interface Proj {
  kind: "missile" | "kab" | "nuke"; side: Side; x: number; y: number; tx: number; ty: number;
  speed: number; r: number; dmg: number; friendly: boolean; dx: number; dy: number;
}
interface Shot { x1: number; y1: number; x2: number; y2: number; t: number; side: Side }
interface Fx { x: number; y: number; r: number; t: number; dur: number }
interface Msg { text: string; t: number }
type Target = Unit | Hq;

const TYPES: Record<Exclude<UnitKind, "shahed">, UnitType> = {
  tank:  { name: "Танк",     cost: 100, hp: 120, speed: 45,  range: 140, dmg: 18, cd: 1.2, dom: "ground", sight: 260, hits: ["ground", "sea"] },
  plane: { name: "Літак",    cost: 250, hp: 80,  speed: 150, range: 120, dmg: 12, cd: 0.4, dom: "air",    sight: 500, hits: ["ground", "sea", "air"] },
  ship:  { name: "Корабель", cost: 300, hp: 400, speed: 30,  range: 260, dmg: 30, cd: 1.5, dom: "sea",    sight: 400, hits: ["ground", "sea", "air"] },
};
const SHAHED = { hp: 20, speed: 70 };
const STRIKES: Record<StrikeKind, { name: string; cost: number; r: number }> = {
  shahed:  { name: "Шахеди x3", cost: 90,   r: 30 },
  missile: { name: "Ракета",    cost: 200,  r: 70 },
  kab:     { name: "КАБ",       cost: 150,  r: 55 },
  nuke:    { name: "Ядерка",    cost: 1500, r: 230 },
};
const KEYS: Record<string, { type: "unit"; kind: Exclude<UnitKind, "shahed"> } | { type: "strike"; kind: StrikeKind }> = {
  "1": { type: "unit", kind: "tank" }, "2": { type: "unit", kind: "plane" }, "3": { type: "unit", kind: "ship" },
  "4": { type: "strike", kind: "shahed" }, "5": { type: "strike", kind: "missile" },
  "6": { type: "strike", kind: "kab" },    "7": { type: "strike", kind: "nuke" },
};
const ORDER = ["1", "2", "3", "4", "5", "6", "7"];
const COL: Record<Side, string> = { blue: "64,153,255", red: "255,82,77" };

interface State {
  units: Unit[]; shots: Shot[]; projs: Proj[]; fx: Fx[]; msgs: Msg[];
  money: Record<Side, number>; hq: Record<Side, Hq>; nukeCd: Record<Side, number>;
  armed: StrikeKind | null; over: "win" | "lose" | null; paused: boolean;
  aiT: number; aiSave: boolean; t: number; flash: number;
}

// ---------------------------------------------------------------- допоміжне
const seaY = (x: number): number => 500 + Math.sin(x * 0.008) * 40 + Math.sin(x * 0.021) * 15;
const dist = (ax: number, ay: number, bx: number, by: number): number => Math.hypot(ax - bx, ay - by);
const enemy = (s: Side): Side => (s === "blue" ? "red" : "blue");
const rnd = (a: number, b: number): number => a + Math.random() * (b - a);
const isUnit = (t: Target): t is Unit => "kind" in t;

let S: State;
let mouse = { x: 0, y: 0 };

function say(text: string): void {
  S.msgs.unshift({ text, t: 5 });
  if (S.msgs.length > 6) S.msgs.pop();
}

function canStand(dom: Dom, x: number, y: number): boolean {
  if (x < 5 || x > W - 5 || y < 40 || y > H - 5) return false;
  if (dom === "air") return true;
  const sea = y > seaY(x);
  return dom === "sea" ? sea : !sea;
}

function newGame(): void {
  S = {
    units: [], shots: [], projs: [], fx: [], msgs: [],
    money: { blue: 500, red: 500 },
    hq: {
      blue: { x: 90, y: 260, hp: 2500, max: 2500, cd: 0 },
      red: { x: 1190, y: 260, hp: 2500, max: 2500, cd: 0 },
    },
    nukeCd: { blue: 0, red: 0 },
    armed: null, over: null, paused: false,
    aiT: 3, aiSave: false, t: 0, flash: 0,
  };
  say("Керування: 1-3 купити, 4-7 обрати удар і клікнути по карті");
}

// ---------------------------------------------------------------- юніти та вибухи
function spawn(kind: UnitKind, side: Side, x: number, y: number): Unit {
  const hp = kind === "shahed" ? SHAHED.hp : TYPES[kind].hp;
  const u: Unit = { kind, side, x, y, hp, cd: 0, angle: side === "blue" ? 0 : Math.PI, tx: x, ty: y };
  S.units.push(u);
  return u;
}

function spawnPoint(kind: UnitKind, side: Side): [number, number] {
  const h = S.hq[side];
  const dir = side === "blue" ? 1 : -1;
  if (kind === "ship") return [h.x + dir * 50, 640 + rnd(-30, 30)];
  return [h.x + dir * (50 + rnd(0, 20)), h.y + rnd(-60, 60)];
}

function addFx(x: number, y: number, r: number): void {
  S.fx.push({ x, y, r, t: 0, dur: 0.6 });
}

function explode(x: number, y: number, r: number, dmg: number, side: Side, friendly: boolean): void {
  addFx(x, y, r);
  for (const u of S.units) {
    if (u.hp > 0 && (friendly || u.side !== side)) {
      const d = dist(x, y, u.x, u.y);
      if (d <= r) u.hp -= dmg * (1 - (0.5 * d) / r);
    }
  }
  for (const s of ["blue", "red"] as Side[]) {
    if (friendly || s !== side) {
      const h = S.hq[s];
      const d = dist(x, y, h.x, h.y);
      if (d <= r + 30) h.hp -= dmg * (1 - (0.5 * d) / (r + 30));
    }
  }
}

function planeNear(side: Side, x: number, y: number, maxd: number): Unit | null {
  let best: Unit | null = null;
  let bd = maxd;
  for (const u of S.units) {
    if (u.side === side && u.kind === "plane" && u.hp > 0) {
      const d = dist(u.x, u.y, x, y);
      if (d < bd) { best = u; bd = d; }
    }
  }
  return best;
}

function launch(kind: StrikeKind, side: Side, tx: number, ty: number): string | null {
  const cost = STRIKES[kind].cost;
  if (S.money[side] < cost) return "Недостатньо грошей";
  const h = S.hq[side];
  if (kind === "shahed") {
    for (let i = 0; i < 3; i++) {
      const u = spawn("shahed", side, h.x + rnd(-20, 20), h.y + rnd(-40, 40));
      u.tx = tx + rnd(-30, 30);
      u.ty = ty + rnd(-30, 30);
    }
  } else if (kind === "missile") {
    S.projs.push({ kind, side, x: h.x, y: h.y, tx, ty, speed: 330, r: 70, dmg: 160, friendly: false, dx: 1, dy: 0 });
  } else if (kind === "kab") {
    const p = planeNear(side, tx, ty, 400);
    if (!p) return "КАБ: потрібен свій літак ближче 400 до цілі";
    S.projs.push({ kind, side, x: p.x, y: p.y, tx, ty, speed: 220, r: 55, dmg: 130, friendly: false, dx: 1, dy: 0 });
  } else {
    if (S.nukeCd[side] > 0) return "Ядерка ще перезаряджається";
    const d = Math.max(dist(h.x, h.y, tx, ty), 1);
    S.projs.push({ kind, side, x: h.x, y: h.y, tx, ty, speed: d / 3, r: 230, dmg: 800, friendly: true, dx: 1, dy: 0 });
    S.nukeCd[side] = 45;
    say((side === "blue" ? "Ви запустили" : "Ворог запустив") + " ЯДЕРНУ РАКЕТУ! Приліт за 3 с");
  }
  S.money[side] -= cost;
  return null;
}

// ---------------------------------------------------------------- поведінка юнітів
function moveToward(u: Unit, speed: number, dom: Dom, tx: number, ty: number, dt: number): void {
  const a = Math.atan2(ty - u.y, tx - u.x);
  for (const off of [0, 0.6, -0.6, 1.2, -1.2, 1.8, -1.8]) {
    const aa = a + off;
    const nx = u.x + Math.cos(aa) * speed * dt;
    const ny = u.y + Math.sin(aa) * speed * dt;
    if (canStand(dom, nx, ny)) { u.x = nx; u.y = ny; u.angle = aa; return; }
  }
}

function updateUnit(u: Unit, dt: number): void {
  u.cd -= dt;
  if (u.kind === "shahed") {
    const d = dist(u.x, u.y, u.tx, u.ty);
    const step = SHAHED.speed * dt;
    u.angle = Math.atan2(u.ty - u.y, u.tx - u.x);
    if (d <= step + 3) {
      explode(u.x, u.y, 45, 85, u.side, false);
      u.hp = 0;
    } else {
      u.x += ((u.tx - u.x) / d) * step;
      u.y += ((u.ty - u.y) / d) * step;
    }
    return;
  }
  const T = TYPES[u.kind];
  const foe = S.hq[enemy(u.side)];
  let tgt: Target | null = null;
  let td = T.sight;
  for (const e of S.units) {
    if (e.side === u.side || e.hp <= 0) continue;
    const edom: Dom = e.kind === "shahed" ? "air" : TYPES[e.kind].dom;
    if (!T.hits.includes(edom)) continue;
    const d = dist(u.x, u.y, e.x, e.y);
    if (d < td) { tgt = e; td = d; }
  }
  const dh = dist(u.x, u.y, foe.x, foe.y) - 30;
  if (dh < td) { tgt = foe; td = dh; }

  let gx = foe.x;
  let gy = foe.y;
  if (T.dom === "sea") {
    gx = foe.x - (u.side === "blue" ? 1 : -1) * 40;
    gy = seaY(gx) + 20;
  }

  if (tgt && td <= T.range) {
    u.angle = Math.atan2(tgt.y - u.y, tgt.x - u.x);
    if (u.cd <= 0) {
      tgt.hp -= T.dmg;
      u.cd = T.cd;
      S.shots.push({ x1: u.x, y1: u.y, x2: tgt.x, y2: tgt.y, t: 0.1, side: u.side });
    }
  } else {
    let tx = gx;
    let ty = gy;
    if (tgt && isUnit(tgt)) {
      const tdom: Dom = tgt.kind === "shahed" ? "air" : TYPES[tgt.kind].dom;
      if (T.dom === "air" || tdom === T.dom) { tx = tgt.x; ty = tgt.y; }
    }
    moveToward(u, T.speed, T.dom, tx, ty, dt);
  }
}

function updateHQ(side: Side, h: Hq, dt: number): void {
  h.cd -= dt;
  if (h.cd > 0) return;
  let best: Unit | null = null;
  let bd = HQ_RANGE;
  for (const e of S.units) {
    if (e.side !== side && e.hp > 0) {
      const d = dist(h.x, h.y, e.x, e.y);
      if (d < bd) { best = e; bd = d; }
    }
  }
  if (best) {
    best.hp -= 8;
    h.cd = 0.4;
    S.shots.push({ x1: h.x, y1: h.y, x2: best.x, y2: best.y, t: 0.1, side });
  }
}

// ---------------------------------------------------------------- ШІ (червоні)
function aiTarget(side: Side): [number, number] {
  const foe = enemy(side);
  const list = S.units.filter((u) => u.side === foe && u.hp > 0 && u.kind !== "shahed");
  if (list.length === 0) return [S.hq[foe].x, S.hq[foe].y];
  const u = list[Math.floor(Math.random() * list.length)]!;
  return [u.x, u.y];
}

function aiCluster(side: Side): [number, number, number] {
  const foe = enemy(side);
  const mine = S.units.filter((u) => u.side === foe && u.hp > 0);
  let bx = 0, by = 0, bn = 0;
  for (const a of mine) {
    const n = mine.filter((b) => dist(a.x, a.y, b.x, b.y) < 200).length;
    if (n > bn) { bx = a.x; by = a.y; bn = n; }
  }
  return [bx, by, bn];
}

function aiThink(side: Side): void {
  const m = S.money[side];
  if (S.aiSave) {
    if (m >= 1500 && S.nukeCd[side] <= 0) {
      let [x, y, n] = aiCluster(side);
      if (n < 3) { x = S.hq[enemy(side)].x; y = S.hq[enemy(side)].y; }
      launch("nuke", side, x, y);
      S.aiSave = false;
    }
    return;
  }
  const r = Math.random();
  if (S.nukeCd[side] <= 0 && m >= 500 && r < 0.08) { S.aiSave = true; return; }
  if (m >= 200 && r < 0.15) { const [x, y] = aiTarget(side); launch("missile", side, x, y); }
  else if (m >= 90 && r < 0.28) { const [x, y] = aiTarget(side); launch("shahed", side, x, y); }
  else if (m >= 150 && r < 0.36) { const [x, y] = aiTarget(side); launch("kab", side, x, y); }
  else {
    const pick = Math.random();
    const kind = pick < 0.55 ? "tank" : pick < 0.8 ? "plane" : "ship";
    if (m >= TYPES[kind].cost) {
      S.money[side] -= TYPES[kind].cost;
      const [x, y] = spawnPoint(kind, side);
      spawn(kind, side, x, y);
    }
  }
}

// ---------------------------------------------------------------- оновлення
function update(dt: number): void {
  for (let i = S.msgs.length - 1; i >= 0; i--) {
    S.msgs[i]!.t -= dt;
    if (S.msgs[i]!.t <= 0) S.msgs.splice(i, 1);
  }
  if (S.paused || S.over) return;

  S.t += dt;
  for (const side of ["blue", "red"] as Side[]) {
    S.money[side] += INCOME * dt;
    S.nukeCd[side] = Math.max(0, S.nukeCd[side] - dt);
  }
  S.aiT -= dt;
  if (S.aiT <= 0) { S.aiT = rnd(2.5, 4.5); aiThink("red"); }

  for (const u of S.units) updateUnit(u, dt);
  updateHQ("blue", S.hq.blue, dt);
  updateHQ("red", S.hq.red, dt);

  S.units = S.units.filter((u) => {
    if (u.hp > 0) return true;
    addFx(u.x, u.y, 16);
    return false;
  });

  S.projs = S.projs.filter((p) => {
    const d = dist(p.x, p.y, p.tx, p.ty);
    const step = p.speed * dt;
    if (d <= step) {
      explode(p.tx, p.ty, p.r, p.dmg, p.side, p.friendly);
      if (p.kind === "nuke") S.flash = 1;
      return false;
    }
    p.dx = (p.tx - p.x) / d;
    p.dy = (p.ty - p.y) / d;
    p.x += p.dx * step;
    p.y += p.dy * step;
    return true;
  });

  for (const s of S.shots) s.t -= dt;
  S.shots = S.shots.filter((s) => s.t > 0);
  for (const f of S.fx) f.t += dt;
  S.fx = S.fx.filter((f) => f.t < f.dur);
  S.flash = Math.max(0, S.flash - dt * 1.2);

  if (S.hq.red.hp <= 0) S.over = "win";
  else if (S.hq.blue.hp <= 0) S.over = "lose";
}

// ---------------------------------------------------------------- малювання
const canvas = document.getElementById("game") as HTMLCanvasElement;
const ctx = canvas.getContext("2d")!;
canvas.width = W;
canvas.height = H;
const FONT = '14px "Segoe UI", system-ui, sans-serif';
const FONT_BIG = 'bold 44px "Segoe UI", system-ui, sans-serif';

// карта малюється один раз у окремий canvas
const mapCv = document.createElement("canvas");
mapCv.width = W;
mapCv.height = H;
(function buildMap(): void {
  const g = mapCv.getContext("2d")!;
  g.fillStyle = "#3b5a38";
  g.fillRect(0, 0, W, H);
  let seed = 7;
  const rand = (): number => { seed = (seed * 16807) % 2147483647; return seed / 2147483647; };
  for (let i = 0; i < 70; i++) {
    g.fillStyle = "rgba(69,102,64,0.8)";
    g.beginPath();
    g.arc(rand() * W, rand() * 520, 20 + rand() * 40, 0, Math.PI * 2);
    g.fill();
  }
  g.fillStyle = "#1f4780";
  for (let x = 0; x <= W; x += 8) g.fillRect(x, seaY(x), 8, H - seaY(x));
  g.strokeStyle = "#d1c280";
  g.lineWidth = 3;
  g.beginPath();
  g.moveTo(0, seaY(0));
  for (let x = 8; x <= W; x += 8) g.lineTo(x, seaY(x));
  g.stroke();
  g.strokeStyle = "rgba(255,255,255,0.05)";
  g.lineWidth = 1;
  for (let x = 0; x <= W; x += 80) { g.beginPath(); g.moveTo(x, 0); g.lineTo(x, H); g.stroke(); }
  for (let y = 0; y <= H; y += 80) { g.beginPath(); g.moveTo(0, y); g.lineTo(W, y); g.stroke(); }
})();

function poly(pts: number[]): void {
  ctx.beginPath();
  ctx.moveTo(pts[0]!, pts[1]!);
  for (let i = 2; i < pts.length; i += 2) ctx.lineTo(pts[i]!, pts[i + 1]!);
  ctx.closePath();
  ctx.fill();
}

function drawUnit(u: Unit): void {
  ctx.save();
  ctx.translate(u.x, u.y);
  ctx.rotate(u.angle);
  ctx.fillStyle = `rgb(${COL[u.side]})`;
  if (u.kind === "tank") {
    ctx.fillRect(-9, -6, 18, 12);
    ctx.strokeStyle = "rgba(255,255,255,0.9)";
    ctx.lineWidth = 3;
    ctx.beginPath(); ctx.moveTo(0, 0); ctx.lineTo(15, 0); ctx.stroke();
  } else if (u.kind === "plane") {
    poly([13, 0, -8, -9, -4, 0, -8, 9]);
  } else if (u.kind === "ship") {
    poly([18, 0, 10, -7, -15, -7, -15, 7, 10, 7]);
    ctx.fillStyle = "rgba(255,255,255,0.9)";
    ctx.beginPath(); ctx.arc(2, 0, 4, 0, Math.PI * 2); ctx.fill();
  } else {
    poly([7, 0, -6, -6, -6, 6]);
  }
  ctx.restore();
  const max = u.kind === "shahed" ? SHAHED.hp : TYPES[u.kind].hp;
  if (u.hp < max) {
    ctx.fillStyle = "rgba(0,0,0,0.7)"; ctx.fillRect(u.x - 12, u.y - 17, 24, 4);
    ctx.fillStyle = "#4dff4d"; ctx.fillRect(u.x - 12, u.y - 17, (24 * Math.max(u.hp, 0)) / max, 4);
  }
}

function drawHq(side: Side, h: Hq): void {
  const c = COL[side];
  ctx.strokeStyle = `rgba(${c},0.15)`; ctx.lineWidth = 1;
  ctx.beginPath(); ctx.arc(h.x, h.y, HQ_RANGE, 0, Math.PI * 2); ctx.stroke();
  ctx.fillStyle = `rgba(${c},0.55)`; ctx.fillRect(h.x - 22, h.y - 22, 44, 44);
  ctx.strokeStyle = `rgb(${c})`; ctx.lineWidth = 3; ctx.strokeRect(h.x - 22, h.y - 22, 44, 44);
  ctx.lineWidth = 2;
  ctx.beginPath(); ctx.moveTo(h.x, h.y - 22); ctx.lineTo(h.x, h.y - 40); ctx.stroke();
  ctx.fillStyle = `rgb(${c})`; poly([h.x, h.y - 40, h.x + 16, h.y - 34, h.x, h.y - 28]);
  ctx.fillStyle = "rgba(0,0,0,0.7)"; ctx.fillRect(h.x - 40, h.y + 30, 80, 8);
  ctx.fillStyle = `rgb(${c})`; ctx.fillRect(h.x - 40, h.y + 30, (80 * Math.max(h.hp, 0)) / h.max, 8);
  ctx.fillStyle = "#fff"; ctx.textAlign = "center";
  ctx.fillText(String(Math.max(0, Math.floor(h.hp))), h.x, h.y + 52);
  ctx.textAlign = "left";
}

function draw(): void {
  ctx.drawImage(mapCv, 0, 0);
  ctx.font = FONT;
  ctx.textBaseline = "alphabetic";

  drawHq("blue", S.hq.blue);
  drawHq("red", S.hq.red);
  for (const u of S.units) drawUnit(u);

  for (const p of S.projs) {
    if (p.kind === "nuke") {
      const left = dist(p.x, p.y, p.tx, p.ty) / p.speed;
      const pulse = 0.5 + 0.5 * Math.sin(S.t * 10);
      ctx.fillStyle = `rgba(255,26,26,${0.2 + 0.2 * pulse})`;
      ctx.beginPath(); ctx.arc(p.tx, p.ty, p.r, 0, Math.PI * 2); ctx.fill();
      ctx.strokeStyle = "#ff4040"; ctx.lineWidth = 2;
      ctx.beginPath(); ctx.arc(p.tx, p.ty, p.r, 0, Math.PI * 2); ctx.stroke();
      ctx.fillStyle = "#fff"; ctx.textAlign = "center"; ctx.fillText(left.toFixed(1), p.tx, p.ty + 5); ctx.textAlign = "left";
      ctx.fillStyle = "#fffacc"; ctx.beginPath(); ctx.arc(p.x, p.y, 6, 0, Math.PI * 2); ctx.fill();
    } else {
      ctx.strokeStyle = "rgba(255,204,102,0.6)"; ctx.lineWidth = 2;
      ctx.beginPath(); ctx.moveTo(p.x, p.y); ctx.lineTo(p.x - p.dx * 16, p.y - p.dy * 16); ctx.stroke();
      ctx.fillStyle = `rgb(${COL[p.side]})`;
      if (p.kind === "kab") ctx.fillRect(p.x - 3, p.y - 3, 6, 6);
      else { ctx.beginPath(); ctx.arc(p.x, p.y, 3.5, 0, Math.PI * 2); ctx.fill(); }
    }
  }

  for (const s of S.shots) {
    ctx.strokeStyle = `rgba(${COL[s.side]},${0.4 + s.t * 5})`; ctx.lineWidth = 1;
    ctx.beginPath(); ctx.moveTo(s.x1, s.y1); ctx.lineTo(s.x2, s.y2); ctx.stroke();
  }
  for (const f of S.fx) {
    const k = f.t / f.dur;
    const r = f.r * (0.3 + 0.7 * k);
    ctx.fillStyle = `rgba(255,153,51,${(1 - k) * 0.5})`;
    ctx.beginPath(); ctx.arc(f.x, f.y, r, 0, Math.PI * 2); ctx.fill();
    ctx.strokeStyle = `rgba(255,230,128,${1 - k})`; ctx.lineWidth = 1;
    ctx.beginPath(); ctx.arc(f.x, f.y, r, 0, Math.PI * 2); ctx.stroke();
  }

  if (S.armed && !S.over) {
    ctx.strokeStyle = "rgba(255,255,77,0.9)"; ctx.lineWidth = 1;
    ctx.beginPath(); ctx.arc(mouse.x, mouse.y, STRIKES[S.armed].r, 0, Math.PI * 2);
    ctx.moveTo(mouse.x - 8, mouse.y); ctx.lineTo(mouse.x + 8, mouse.y);
    ctx.moveTo(mouse.x, mouse.y - 8); ctx.lineTo(mouse.x, mouse.y + 8);
    ctx.stroke();
  }

  // інтерфейс
  ctx.fillStyle = "rgba(0,0,0,0.65)"; ctx.fillRect(0, 0, W, 34);
  ctx.fillStyle = "#fff";
  ctx.fillText(`Гроші: ${Math.floor(S.money.blue)} $   (+${INCOME}/с)`, 14, 22);
  ctx.fillText(S.nukeCd.blue > 0 ? `Ядерка: ${Math.ceil(S.nukeCd.blue)}с` : "Ядерка: готова", 260, 22);
  ctx.textAlign = "right";
  ctx.fillText("P — пауза   R — заново   ПКМ/Esc — скасувати удар", W - 14, 22);
  ctx.textAlign = "left";

  ctx.fillStyle = "rgba(0,0,0,0.7)"; ctx.fillRect(0, H - 64, W, 64);
  const cw = W / 7;
  ORDER.forEach((key, i) => {
    const k = KEYS[key]!;
    const info = k.type === "unit" ? TYPES[k.kind] : STRIKES[k.kind];
    const x = i * cw;
    if (S.armed === k.kind && k.type === "strike") {
      ctx.fillStyle = "rgba(255,255,77,0.25)"; ctx.fillRect(x + 3, H - 61, cw - 6, 58);
    }
    ctx.fillStyle = S.money.blue >= info.cost ? "#fff" : "#8c8c8c";
    ctx.fillText(`[${key}] ${info.name}`, x + 12, H - 38);
    ctx.fillText(`${info.cost} $${k.type === "strike" ? "  · клік" : ""}`, x + 12, H - 16);
  });

  S.msgs.forEach((m, i) => {
    ctx.fillStyle = `rgba(255,255,255,${Math.min(1, m.t)})`;
    ctx.fillText(m.text, 14, 56 + i * 18);
  });

  if (S.flash > 0) { ctx.fillStyle = `rgba(255,255,255,${S.flash})`; ctx.fillRect(0, 0, W, H); }
  if (S.over) {
    ctx.fillStyle = "rgba(0,0,0,0.6)"; ctx.fillRect(0, 0, W, H);
    ctx.font = FONT_BIG; ctx.textAlign = "center";
    ctx.fillStyle = S.over === "win" ? "#66ff80" : "#ff6666";
    ctx.fillText(S.over === "win" ? "ПЕРЕМОГА!" : "ПОРАЗКА", W / 2, H / 2);
    ctx.font = FONT; ctx.fillStyle = "#fff";
    ctx.fillText("Натисніть R, щоб почати заново", W / 2, H / 2 + 36);
    ctx.textAlign = "left";
  } else if (S.paused) {
    ctx.font = FONT_BIG; ctx.textAlign = "center"; ctx.fillStyle = "#fff";
    ctx.fillText("ПАУЗА", W / 2, H / 2);
    ctx.font = FONT; ctx.textAlign = "left";
  }
}

// ---------------------------------------------------------------- ввід
function toGame(ev: MouseEvent): { x: number; y: number } {
  const r = canvas.getBoundingClientRect();
  return { x: ((ev.clientX - r.left) * W) / r.width, y: ((ev.clientY - r.top) * H) / r.height };
}

window.addEventListener("keydown", (e) => {
  const key = e.key.toLowerCase();
  if (key === "r") { newGame(); return; }
  if (key === "p") { S.paused = !S.paused; return; }
  if (key === "escape") { S.armed = null; return; }
  const k = KEYS[key];
  if (!k || S.over) return;
  if (k.type === "unit") {
    const cost = TYPES[k.kind].cost;
    if (S.money.blue >= cost) {
      S.money.blue -= cost;
      const [x, y] = spawnPoint(k.kind, "blue");
      spawn(k.kind, "blue", x, y);
    } else say("Недостатньо грошей");
  } else {
    S.armed = S.armed === k.kind ? null : k.kind;
  }
});

canvas.addEventListener("mousemove", (e) => { mouse = toGame(e); });
canvas.addEventListener("contextmenu", (e) => { e.preventDefault(); S.armed = null; });
canvas.addEventListener("mousedown", (e) => {
  if (e.button !== 0 || !S.armed || S.over || S.paused) return;
  const p = toGame(e);
  const err = launch(S.armed, "blue", p.x, p.y);
  if (err) say(err);
  else if (S.armed === "nuke") S.armed = null;
});

// ---------------------------------------------------------------- цикл
newGame();
let last = performance.now();
function frame(now: number): void {
  const dt = Math.min((now - last) / 1000, 0.05);
  last = now;
  update(dt);
  draw();
  requestAnimationFrame(frame);
}
requestAnimationFrame(frame);
