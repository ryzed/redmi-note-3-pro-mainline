-- kenzo (Xiaomi Redmi Note 3 Pro): гнездо наушников выбирает профиль встроенной карты (тикет 26b10-01).
--
-- Динамик и наушники в UCM — два профиля карты («HiFi (Mic, Speaker)», «HiFi (Headphones, Mic)»): смена
-- профиля переоткрывает поток, а переключение пути кодека под открытым потоком переполняет порт SLIMbus RX
-- и выход молчит (26b09-37). Штатный WirePlumber по гнезду меняет выход только внутри профиля, а профиль
-- держит прежний, пока тот доступен, — поэтому решает этот хук:
--   * наушники в гнезде (UCM JackControl «Headset Jack» → маршрут Headphones available = yes) — профиль
--     с Headphones, вынуты (no) — со Speaker; гнезда нет (unknown: ядро без MBHC) — решает штатная логика;
--   * при каждом движении штекера (изменилась доступность маршрута Headphones) — выбор профиля заново.
-- Ручной выбор (set-output, pavucontrol) действует до следующего движения штекера.

cutils = require ("common-utils")
log = Log.open_topic ("s-device")

local CARD = "alsa_card.platform-sound"

local function isKenzoCard (device)
  local name = device.properties ["device.name"] or ""
  return name:sub (1, #CARD) == CARD
end

-- "yes" / "no" / "unknown" для маршрута наушников; nil — такого маршрута нет
local function headphonesAvailable (device)
  for p in device:iterate_params ("EnumRoute") do
    local route = cutils.parseParam (p, "EnumRoute")
    if route and route.direction == "Output" and route.name and
        route.name:find ("Headphones", 1, true) then
      return route.available
    end
  end
  return nil
end

local function findProfile (device, word)
  for p in device:iterate_params ("EnumProfile") do
    local profile = cutils.parseParam (p, "EnumProfile")
    if profile and profile.name and profile.name:find (word, 1, true) then
      return profile
    end
  end
  return nil
end

SimpleEventHook {
  name = "kenzo/find-jack-profile",
  before = { "device/find-stored-profile", "device/find-preferred-profile",
             "device/find-best-profile" },
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "select-profile" },
    },
  },
  execute = function (event)
    if event:get_data ("selected-profile") then
      return
    end
    local device = event:get_subject ()
    if not isKenzoCard (device) then
      return
    end
    local avail = headphonesAvailable (device)
    if avail ~= "yes" and avail ~= "no" then
      return
    end
    local profile = findProfile (device, avail == "yes" and "Headphones" or "Speaker")
    if profile then
      log:info (device, "kenzo: headphones " .. avail .. " -> profile " .. profile.name)
      event:set_data ("selected-profile", profile)
    end
  end
}:register ()

local last_avail = {}   -- bound-id карты -> последняя доступность наушников

SimpleEventHook {
  name = "kenzo/jack-changed",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "device-params-changed" },
      Constraint { "event.subject.param-id", "=", "EnumRoute" },
    },
  },
  execute = function (event)
    local device = event:get_subject ()
    if not isKenzoCard (device) then
      return
    end
    local avail = headphonesAvailable (device)
    local id = device ["bound-id"]
    if avail == last_avail [id] then
      return
    end
    log:info (device, "kenzo: headphones " .. tostring (last_avail [id]) .. " -> " .. tostring (avail))
    last_avail [id] = avail
    event:get_source ():call ("push-event", "select-profile", device, nil)
  end
}:register ()
