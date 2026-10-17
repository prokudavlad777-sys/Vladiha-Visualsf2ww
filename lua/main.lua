-- 2D ВІЙНА на карті (Lua / LÖVE 11.x)
-- Танки, літаки, кораблі, шахеди, ракети, КАБи та ядерні удари.
-- Ви граєте за СИНІХ (ліворуч), ШІ керує ЧЕРВОНИМИ (праворуч).

local W, H = 1280, 720
local atan2 = math.atan2 or math.atan

---------------------------------------------------------------- дані
local TYPES = {
  tank   = {name = "Танк",     cost = 100, hp = 120, speed = 45,  range = 140, dmg = 18, cd = 1.2, dom = "ground", sight = 260,
            hits = {ground = true, sea = true}},
  plane  = {name = "Літак",    cost = 250, hp = 80,  speed = 150, range = 120, dmg = 12, cd = 0.4, dom = "air",    sight = 500,
            hits = {ground = true, sea = true, air = true}},
  ship   = {name = "Корабель", cost = 300, hp = 400, speed = 30,  range = 260, dmg = 30, cd = 1.5, dom = "sea",    sight = 400,
            hits = {ground = true, sea = true, air = true}},
  shahed = {name = "Шахед",    hp = 20, speed = 70, dom = "air"},
}
local STRIKES = {
  shahed  = {name = "Шахеди x3", cost = 90,   r = 30},
  missile = {name = "Ракета",    cost = 200,  r = 70},
  kab     = {name = "КАБ",       cost = 150,  r = 55},
  nuke    = {name = "Ядерка",     cost = 1500, r = 230},
}
local KEYS = {
  ["1"] = {"unit", "tank"},   ["2"] = {"unit", "plane"},   ["3"] = {"unit", "ship"},
  ["4"] = {"strike", "shahed"}, ["5"] = {"strike", "missile"},
  ["6"] = {"strike", "kab"},    ["7"] = {"strike", "nuke"},
}
local ORDER = {"1", "2", "3", "4", "5", "6", "7"}
local COL = {blue = {0.25, 0.6, 1}, red = {1, 0.32, 0.3}}
local INCOME = 14
local HQ_RANGE = 230

local S                       -- стан гри
local mapCanvas, font, fontBig

---------------------------------------------------------------- допоміжне
local function seaY(x) return 500 + math.sin(x * 0.008) * 40 + math.sin(x * 0.021) * 15 end
local function dist(ax, ay, bx, by) return math.sqrt((ax - bx) ^ 2 + (ay - by) ^ 2) end
local function enemy(side) return side == "blue" and "red" or "blue" end

local function say(text)
  table.insert(S.msgs, 1, {text = text, t = 5})
  if #S.msgs > 6 then table.remove(S.msgs) end
end

local function canStand(dom, x, y)
  if x < 5 or x > W - 5 or y < 40 or y > H - 5 then return false end
  if dom == "air" then return true end
  local sea = y > seaY(x)
  if dom == "sea" then return sea end
  return not sea
end

local function newGame()
  S = {
    units = {}, shots = {}, projs = {}, fx = {}, msgs = {},
    money = {blue = 500, red = 500},
    hq = {
      blue = {x = 90,   y = 260, hp = 2500, max = 2500, cd = 0},
      red  = {x = 1190, y = 260, hp = 2500, max = 2500, cd = 0},
    },
    nukeCd = {blue = 0, red = 0},
    armed = nil, over = nil, paused = false,
    aiT = 3, aiSave = false, t = 0, flash = 0,
  }
  say("Керування: 1-3 купити, 4-7 обрати удар і клікнути по карті")
end

local function buildMap()
  mapCanvas = love.graphics.newCanvas(W, H)
  love.graphics.setCanvas(mapCanvas)
  love.graphics.clear(0.23, 0.35, 0.22)
  local rng = love.math.newRandomGenerator(7)
  for _ = 1, 70 do
    local x, y = rng:random(0, W), rng:random(0, 520)
    love.graphics.setColor(0.27, 0.40, 0.25, 0.8)
    love.graphics.circle("fill", x, y, rng:random(20, 60))
  end
  love.graphics.setColor(0.12, 0.28, 0.5)
  for x = 0, W, 8 do
    local sy = seaY(x)
    love.graphics.rectangle("fill", x, sy, 8, H - sy)
  end
  love.graphics.setColor(0.82, 0.76, 0.5)
  love.graphics.setLineWidth(3)
  for x = 0, W - 8, 8 do love.graphics.line(x, seaY(x), x + 8, seaY(x + 8)) end
  love.graphics.setLineWidth(1)
  love.graphics.setColor(1, 1, 1, 0.05)
  for x = 0, W, 80 do love.graphics.line(x, 0, x, H) end
  for y = 0, H, 80 do love.graphics.line(0, y, W, y) end
  love.graphics.setCanvas()
end

---------------------------------------------------------------- юніти та вибухи
local function spawn(kind, side, x, y)
  local u = {kind = kind, side = side, x = x, y = y, hp = TYPES[kind].hp, cd = 0,
             angle = side == "blue" and 0 or math.pi}
  table.insert(S.units, u)
  return u
end

local function spawnPoint(kind, side)
  local h = S.hq[side]
  local dir = side == "blue" and 1 or -1
  if kind == "ship" then return h.x + dir * 50, 640 + math.random(-30, 30) end
  return h.x + dir * (50 + math.random(0, 20)), h.y + math.random(-60, 60)
end

local function addFx(x, y, r)
  table.insert(S.fx, {x = x, y = y, r = r, t = 0, dur = 0.6})
end

local function explode(x, y, r, dmg, side, friendly)
  addFx(x, y, r)
  for _, u in ipairs(S.units) do
    if u.hp > 0 and (friendly or u.side ~= side) then
      local d = dist(x, y, u.x, u.y)
      if d <= r then u.hp = u.hp - dmg * (1 - 0.5 * d / r) end
    end
  end
  for s, h in pairs(S.hq) do
    if friendly or s ~= side then
      local d = dist(x, y, h.x, h.y)
      if d <= r + 30 then h.hp = h.hp - dmg * (1 - 0.5 * d / (r + 30)) end
    end
  end
end

local function planeNear(side, x, y, maxd)
  local best, bd = nil, maxd
  for _, u in ipairs(S.units) do
    if u.side == side and u.kind == "plane" and u.hp > 0 then
      local d = dist(u.x, u.y, x, y)
      if d < bd then best, bd = u, d end
    end
  end
  return best
end

-- повертає true або false, "причина"
local function launch(kind, side, tx, ty)
  local cost = STRIKES[kind].cost
  if S.money[side] < cost then return false, "Недостатньо грошей" end
  local h = S.hq[side]
  if kind == "shahed" then
    for _ = 1, 3 do
      local u = spawn("shahed", side, h.x + math.random(-20, 20), h.y + math.random(-40, 40))
      u.tx, u.ty = tx + math.random(-30, 30), ty + math.random(-30, 30)
    end
  elseif kind == "missile" then
    table.insert(S.projs, {kind = "missile", side = side, x = h.x, y = h.y, tx = tx, ty = ty, speed = 330, r = 70, dmg = 160})
  elseif kind == "kab" then
    local p = planeNear(side, tx, ty, 400)
    if not p then return false, "КАБ: потрібен свій літак ближче 400 до цілі" end
    table.insert(S.projs, {kind = "kab", side = side, x = p.x, y = p.y, tx = tx, ty = ty, speed = 220, r = 55, dmg = 130})
  elseif kind == "nuke" then
    if S.nukeCd[side] > 0 then return false, "Ядерка ще перезаряджається" end
    local d = dist(h.x, h.y, tx, ty)
    table.insert(S.projs, {kind = "nuke", side = side, x = h.x, y = h.y, tx = tx, ty = ty,
                           speed = math.max(d, 1) / 3, r = 230, dmg = 800, friendly = true})
    S.nukeCd[side] = 45
    say((side == "blue" and "Ви запустили" or "Ворог запустив") .. " ЯДЕРНУ РАКЕТУ! Приліт за 3 с")
  end
  S.money[side] = S.money[side] - cost
  return true
end

---------------------------------------------------------------- поведінка юнітів
local function moveToward(u, T, tx, ty, dt)
  local a = atan2(ty - u.y, tx - u.x)
  for _, off in ipairs({0, 0.6, -0.6, 1.2, -1.2, 1.8, -1.8}) do
    local aa = a + off
    local nx, ny = u.x + math.cos(aa) * T.speed * dt, u.y + math.sin(aa) * T.speed * dt
    if canStand(T.dom, nx, ny) then
      u.x, u.y, u.angle = nx, ny, aa
      return
    end
  end
end

local function updateUnit(u, dt)
  local T = TYPES[u.kind]
  u.cd = u.cd - dt

  if u.kind == "shahed" then       -- камікадзе
    local d = dist(u.x, u.y, u.tx, u.ty)
    local step = T.speed * dt
    u.angle = atan2(u.ty - u.y, u.tx - u.x)
    if d <= step + 3 then
      explode(u.x, u.y, 45, 85, u.side)
      u.hp = 0
    else
      u.x = u.x + (u.tx - u.x) / d * step
      u.y = u.y + (u.ty - u.y) / d * step
    end
    return
  end

  local foe = S.hq[enemy(u.side)]
  local tgt, td = nil, T.sight
  for _, e in ipairs(S.units) do
    if e.side ~= u.side and e.hp > 0 and T.hits[TYPES[e.kind].dom] then
      local d = dist(u.x, u.y, e.x, e.y)
      if d < td then tgt, td = e, d end
    end
  end
  local dh = dist(u.x, u.y, foe.x, foe.y) - 30
  if dh < td then tgt, td = foe, dh end

  local gx, gy = foe.x, foe.y
  if T.dom == "sea" then
    gx = foe.x - (u.side == "blue" and 1 or -1) * 40
    gy = seaY(gx) + 20
  end

  if tgt and td <= T.range then
    u.angle = atan2(tgt.y - u.y, tgt.x - u.x)
    if u.cd <= 0 then
      tgt.hp = tgt.hp - T.dmg
      u.cd = T.cd
      table.insert(S.shots, {x1 = u.x, y1 = u.y, x2 = tgt.x, y2 = tgt.y, t = 0.1, side = u.side})
    end
  else
    local tx, ty = gx, gy
    if tgt and tgt.kind and (T.dom == "air" or TYPES[tgt.kind].dom == T.dom) then tx, ty = tgt.x, tgt.y end
    moveToward(u, T, tx, ty, dt)
  end
end

local function updateHQ(side, h, dt)
  h.cd = h.cd - dt
  if h.cd > 0 then return end
  local best, bd = nil, HQ_RANGE
  for _, e in ipairs(S.units) do
    if e.side ~= side and e.hp > 0 then
      local d = dist(h.x, h.y, e.x, e.y)
      if d < bd then best, bd = e, d end
    end
  end
  if best then
    best.hp = best.hp - 8
    h.cd = 0.4
    table.insert(S.shots, {x1 = h.x, y1 = h.y, x2 = best.x, y2 = best.y, t = 0.1, side = side})
  end
end

---------------------------------------------------------------- ШІ (червоні)
local function aiTarget(side)
  local foe, list = enemy(side), {}
  for _, u in ipairs(S.units) do
    if u.side == foe and u.hp > 0 and u.kind ~= "shahed" then list[#list + 1] = u end
  end
  if #list == 0 then return S.hq[foe].x, S.hq[foe].y end
  local u = list[math.random(#list)]
  return u.x, u.y
end

local function aiCluster(side)
  local foe, bx, by, bn = enemy(side), nil, nil, 0
  for _, a in ipairs(S.units) do
    if a.side == foe and a.hp > 0 then
      local n = 0
      for _, b in ipairs(S.units) do
        if b.side == foe and b.hp > 0 and dist(a.x, a.y, b.x, b.y) < 200 then n = n + 1 end
      end
      if n > bn then bx, by, bn = a.x, a.y, n end
    end
  end
  return bx, by, bn
end

local function aiThink(side)
  local m = S.money[side]
  if S.aiSave then
    if m >= 1500 and S.nukeCd[side] <= 0 then
      local x, y, n = aiCluster(side)
      if n < 3 then x, y = S.hq[enemy(side)].x, S.hq[enemy(side)].y end
      launch("nuke", side, x, y)
      S.aiSave = false
    end
    return
  end
  local r = math.random()
  if S.nukeCd[side] <= 0 and m >= 500 and r < 0.08 then S.aiSave = true return end
  if m >= 200 and r < 0.15 then
    local x, y = aiTarget(side); launch("missile", side, x, y)
  elseif m >= 90 and r < 0.28 then
    local x, y = aiTarget(side); launch("shahed", side, x, y)
  elseif m >= 150 and r < 0.36 then
    local x, y = aiTarget(side); launch("kab", side, x, y)
  else
    local pick = math.random()
    local kind = pick < 0.55 and "tank" or (pick < 0.80 and "plane" or "ship")
    if m >= TYPES[kind].cost then
      S.money[side] = m - TYPES[kind].cost
      local x, y = spawnPoint(kind, side)
      spawn(kind, side, x, y)
    end
  end
end

---------------------------------------------------------------- love callbacks
function love.load()
  math.randomseed(os.time())
  font = love.graphics.newFont("DejaVuSans.ttf", 14)
  fontBig = love.graphics.newFont("DejaVuSans.ttf", 44)
  newGame()
  buildMap()
end

function love.update(dt)
  dt = math.min(dt, 0.05)
  for i = #S.msgs, 1, -1 do
    S.msgs[i].t = S.msgs[i].t - dt
    if S.msgs[i].t <= 0 then table.remove(S.msgs, i) end
  end
  if S.paused or S.over then return end

  S.t = S.t + dt
  for _, side in ipairs({"blue", "red"}) do
    S.money[side] = S.money[side] + INCOME * dt
    S.nukeCd[side] = math.max(0, S.nukeCd[side] - dt)
  end

  S.aiT = S.aiT - dt
  if S.aiT <= 0 then
    S.aiT = 2.5 + math.random() * 2
    aiThink("red")
  end

  for _, u in ipairs(S.units) do updateUnit(u, dt) end
  for side, h in pairs(S.hq) do updateHQ(side, h, dt) end

  for i = #S.units, 1, -1 do
    if S.units[i].hp <= 0 then
      addFx(S.units[i].x, S.units[i].y, 16)
      table.remove(S.units, i)
    end
  end

  for i = #S.projs, 1, -1 do
    local p = S.projs[i]
    local d = dist(p.x, p.y, p.tx, p.ty)
    local step = p.speed * dt
    if d <= step then
      explode(p.tx, p.ty, p.r, p.dmg, p.side, p.friendly)
      if p.kind == "nuke" then S.flash = 1 end
      table.remove(S.projs, i)
    else
      p.dx, p.dy = (p.tx - p.x) / d, (p.ty - p.y) / d
      p.x, p.y = p.x + p.dx * step, p.y + p.dy * step
    end
  end

  for i = #S.shots, 1, -1 do
    S.shots[i].t = S.shots[i].t - dt
    if S.shots[i].t <= 0 then table.remove(S.shots, i) end
  end
  for i = #S.fx, 1, -1 do
    S.fx[i].t = S.fx[i].t + dt
    if S.fx[i].t >= S.fx[i].dur then table.remove(S.fx, i) end
  end
  S.flash = math.max(0, S.flash - dt * 1.2)

  if S.hq.red.hp <= 0 then S.over = "win"
  elseif S.hq.blue.hp <= 0 then S.over = "lose" end
end

function love.keypressed(key)
  if key == "r" then newGame() return end
  if key == "p" then S.paused = not S.paused return end
  if key == "escape" then
    if S.armed then S.armed = nil else love.event.quit() end
    return
  end
  local k = KEYS[key]
  if not k or S.over then return end
  if k[1] == "unit" then
    local cost = TYPES[k[2]].cost
    if S.money.blue >= cost then
      S.money.blue = S.money.blue - cost
      local x, y = spawnPoint(k[2], "blue")
      spawn(k[2], "blue", x, y)
    else
      say("Недостатньо грошей")
    end
  else
    S.armed = (S.armed == k[2]) and nil or k[2]
  end
end

function love.mousepressed(x, y, button)
  if button == 2 then S.armed = nil return end
  if button == 1 and S.armed and not S.over and not S.paused then
    local ok, err = launch(S.armed, "blue", x, y)
    if not ok then say(err)
    elseif S.armed == "nuke" then S.armed = nil end
  end
end

---------------------------------------------------------------- малювання
local function drawUnit(u)
  local c = COL[u.side]
  local T = TYPES[u.kind]
  love.graphics.push()
  love.graphics.translate(u.x, u.y)
  love.graphics.rotate(u.angle)
  love.graphics.setColor(c[1], c[2], c[3])
  if u.kind == "tank" then
    love.graphics.rectangle("fill", -9, -6, 18, 12)
    love.graphics.setColor(1, 1, 1, 0.9)
    love.graphics.setLineWidth(3); love.graphics.line(0, 0, 15, 0); love.graphics.setLineWidth(1)
  elseif u.kind == "plane" then
    love.graphics.polygon("fill", 13, 0, -8, -9, -4, 0, -8, 9)
  elseif u.kind == "ship" then
    love.graphics.polygon("fill", 18, 0, 10, -7, -15, -7, -15, 7, 10, 7)
    love.graphics.setColor(1, 1, 1, 0.9); love.graphics.circle("fill", 2, 0, 4)
  else
    love.graphics.polygon("fill", 7, 0, -6, -6, -6, 6)
  end
  love.graphics.pop()
  if T.hp and u.hp < T.hp then
    love.graphics.setColor(0, 0, 0, 0.7); love.graphics.rectangle("fill", u.x - 12, u.y - 17, 24, 4)
    love.graphics.setColor(0.3, 1, 0.3); love.graphics.rectangle("fill", u.x - 12, u.y - 17, 24 * math.max(u.hp, 0) / T.hp, 4)
  end
end

local function drawHQ(side, h)
  local c = COL[side]
  love.graphics.setColor(c[1], c[2], c[3], 0.12)
  love.graphics.circle("line", h.x, h.y, HQ_RANGE)
  love.graphics.setColor(c[1] * 0.6, c[2] * 0.6, c[3] * 0.6)
  love.graphics.rectangle("fill", h.x - 22, h.y - 22, 44, 44)
  love.graphics.setColor(c[1], c[2], c[3])
  love.graphics.setLineWidth(3); love.graphics.rectangle("line", h.x - 22, h.y - 22, 44, 44); love.graphics.setLineWidth(1)
  love.graphics.line(h.x, h.y - 22, h.x, h.y - 40)
  love.graphics.polygon("fill", h.x, h.y - 40, h.x + 16, h.y - 34, h.x, h.y - 28)
  love.graphics.setColor(0, 0, 0, 0.7); love.graphics.rectangle("fill", h.x - 40, h.y + 30, 80, 8)
  love.graphics.setColor(c[1], c[2], c[3]); love.graphics.rectangle("fill", h.x - 40, h.y + 30, 80 * math.max(h.hp, 0) / h.max, 8)
  love.graphics.setColor(1, 1, 1); love.graphics.setFont(font)
  love.graphics.printf(math.max(0, math.floor(h.hp)) .. "", h.x - 40, h.y + 40, 80, "center")
end

function love.draw()
  love.graphics.setColor(1, 1, 1)
  love.graphics.draw(mapCanvas, 0, 0)
  love.graphics.setFont(font)

  for side, h in pairs(S.hq) do drawHQ(side, h) end
  for _, u in ipairs(S.units) do drawUnit(u) end

  for _, p in ipairs(S.projs) do
    if p.kind == "nuke" then
      local left = dist(p.x, p.y, p.tx, p.ty) / p.speed
      local pulse = 0.5 + 0.5 * math.sin(S.t * 10)
      love.graphics.setColor(1, 0.1, 0.1, 0.2 + 0.2 * pulse); love.graphics.circle("fill", p.tx, p.ty, p.r)
      love.graphics.setColor(1, 0.25, 0.25); love.graphics.setLineWidth(2); love.graphics.circle("line", p.tx, p.ty, p.r); love.graphics.setLineWidth(1)
      love.graphics.setColor(1, 1, 1); love.graphics.printf(string.format("%.1f", left), p.tx - 30, p.ty - 8, 60, "center")
      love.graphics.setColor(1, 1, 0.8); love.graphics.circle("fill", p.x, p.y, 6)
    else
      local dx, dy = p.dx or 1, p.dy or 0
      love.graphics.setColor(1, 0.8, 0.4, 0.6); love.graphics.setLineWidth(2)
      love.graphics.line(p.x, p.y, p.x - dx * 16, p.y - dy * 16); love.graphics.setLineWidth(1)
      local c = COL[p.side]; love.graphics.setColor(c[1], c[2], c[3])
      if p.kind == "kab" then love.graphics.rectangle("fill", p.x - 3, p.y - 3, 6, 6)
      else love.graphics.circle("fill", p.x, p.y, 3.5) end
    end
  end

  for _, s in ipairs(S.shots) do
    local c = COL[s.side]
    love.graphics.setColor(c[1], c[2], c[3], 0.4 + s.t * 5)
    love.graphics.line(s.x1, s.y1, s.x2, s.y2)
  end
  for _, f in ipairs(S.fx) do
    local k = f.t / f.dur
    love.graphics.setColor(1, 0.6, 0.2, (1 - k) * 0.5)
    love.graphics.circle("fill", f.x, f.y, f.r * (0.3 + 0.7 * k))
    love.graphics.setColor(1, 0.9, 0.5, 1 - k)
    love.graphics.circle("line", f.x, f.y, f.r * (0.3 + 0.7 * k))
  end

  -- приціл обраного удару
  if S.armed and not S.over then
    local mx, my = love.mouse.getPosition()
    local r = STRIKES[S.armed].r
    love.graphics.setColor(1, 1, 0.3, 0.9); love.graphics.circle("line", mx, my, r)
    love.graphics.line(mx - 8, my, mx + 8, my); love.graphics.line(mx, my - 8, mx, my + 8)
  end

  -- інтерфейс
  love.graphics.setColor(0, 0, 0, 0.65); love.graphics.rectangle("fill", 0, 0, W, 34)
  love.graphics.setColor(1, 1, 1)
  love.graphics.print(string.format("Гроші: %d $   (+%d/с)", math.floor(S.money.blue), INCOME), 14, 8)
  local nk = S.nukeCd.blue > 0 and string.format("Ядерка: %dс", math.ceil(S.nukeCd.blue)) or "Ядерка: готова"
  love.graphics.print(nk, 260, 8)
  love.graphics.printf("P — пауза   R — заново   ПКМ/Esc — скасувати удар", 0, 8, W - 14, "right")

  love.graphics.setColor(0, 0, 0, 0.7); love.graphics.rectangle("fill", 0, H - 64, W, 64)
  local cw = W / 7
  for i, key in ipairs(ORDER) do
    local k = KEYS[key]
    local info = k[1] == "unit" and TYPES[k[2]] or STRIKES[k[2]]
    local x = (i - 1) * cw
    local armed = S.armed == k[2]
    if armed then love.graphics.setColor(1, 1, 0.3, 0.25); love.graphics.rectangle("fill", x + 3, H - 61, cw - 6, 58) end
    local afford = S.money.blue >= info.cost
    love.graphics.setColor(afford and 1 or 0.55, afford and 1 or 0.55, afford and 1 or 0.55)
    love.graphics.print(string.format("[%s] %s", key, info.name), x + 12, H - 54)
    love.graphics.print(info.cost .. " $" .. (k[1] == "strike" and "  · клік" or ""), x + 12, H - 32)
  end

  for i, m in ipairs(S.msgs) do
    love.graphics.setColor(1, 1, 1, math.min(1, m.t))
    love.graphics.print(m.text, 14, 44 + (i - 1) * 18)
  end

  if S.flash > 0 then
    love.graphics.setColor(1, 1, 1, S.flash); love.graphics.rectangle("fill", 0, 0, W, H)
  end
  if S.over then
    love.graphics.setColor(0, 0, 0, 0.6); love.graphics.rectangle("fill", 0, 0, W, H)
    love.graphics.setFont(fontBig)
    if S.over == "win" then love.graphics.setColor(0.4, 1, 0.5) else love.graphics.setColor(1, 0.4, 0.4) end
    love.graphics.printf(S.over == "win" and "ПЕРЕМОГА!" or "ПОРАЗКА", 0, H / 2 - 50, W, "center")
    love.graphics.setFont(font); love.graphics.setColor(1, 1, 1)
    love.graphics.printf("Натисніть R, щоб почати заново", 0, H / 2 + 14, W, "center")
  elseif S.paused then
    love.graphics.setFont(fontBig); love.graphics.setColor(1, 1, 1)
    love.graphics.printf("ПАУЗА", 0, H / 2 - 30, W, "center")
  end
end
