// 🔥 Firebase config
const firebaseConfig = {
  apiKey: "AIzaSyAmUTmxbPzmxQ74i2peGV6IGTQo0rrz-FM",
  authDomain: "bus-tracking-eae81.firebaseapp.com",
  databaseURL: "https://bus-tracking-eae81-default-rtdb.asia-southeast1.firebasedatabase.app",
  projectId: "bus-tracking-eae81",
  storageBucket: "bus-tracking-eae81.firebasestorage.app",
  messagingSenderId: "651200089794",
  appId: "1:651200089794:web:0953aecdd5acb98b95b55b"
};

console.log("Bus tracker script loaded");

firebase.initializeApp(firebaseConfig);
const db = firebase.database();

// 🗺 MAP
var map = L.map('map').setView([0.0, 0.0], 2);

L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png', {
  attribution: "© OpenStreetMap"
}).addTo(map);

// 🚍 BUS ICON (small = professional look)
var busIcon = L.icon({
  iconUrl: "bus1.png",
  iconSize: [50, 50],
  iconAnchor: [17, 17]
});

var marker = null;
const LOCAL_STORAGE_KEY = "busTrackerLastLocation";
const FALLBACK_LAT = 10.106031;
const FALLBACK_LON = 78.643106;

// 📍 ROUTE
var route = L.polyline([], { color: 'blue', weight: 4 }).addTo(map);
var stopMarkers = [];

// 🔄 STATE
let currentLatLng = null;
let animationInterval = null;
let lastAngle = 0;

let prevLat = null;
let prevLon = null;

let lastLat = null;
let lastLon = null;

let totalDistance = 0;
let lastUpdate = null;

let tracking = true;
let routePoints = [];

let busStatus = "STOPPED";
let moveScore = 0;

let speed = 0;
let lastUpdateTime = null;

function saveLastLocation(lat, lon) {
  try {
    window.localStorage.setItem(
      LOCAL_STORAGE_KEY,
      JSON.stringify({ lat, lon, seenAt: Date.now() })
    );
  } catch (e) {
    console.warn("Unable to save last location", e);
  }
}

function isValidLatLon(lat, lon) {
  if (lat === null || lon === null || typeof lat === 'undefined' || typeof lon === 'undefined') return false;
  const parsedLat = Number(lat);
  const parsedLon = Number(lon);
  if (Number.isNaN(parsedLat) || Number.isNaN(parsedLon)) return false;
  if (parsedLat === 0 && parsedLon === 0) return false;
  if (parsedLat < -90 || parsedLat > 90 || parsedLon < -180 || parsedLon > 180) return false;
  return true;
}

function findLatLonDeep(obj) {
  if (!obj || typeof obj !== 'object') return null;
  if (isValidLatLon(obj.lat, obj.lon)) {
    return { lat: Number(obj.lat), lon: Number(obj.lon), raw: obj };
  }

  for (const key in obj) {
    if (!Object.prototype.hasOwnProperty.call(obj, key)) continue;
    const child = obj[key];
    if (child && typeof child === 'object') {
      const found = findLatLonDeep(child);
      if (found) {
        found.key = key;
        return found;
      }
    }
  }

  return null;
}

function parseGpsSnapshot(data) {
  if (!data) return null;
  return findLatLonDeep(data);
}

function loadLastLocation() {
  console.log("Attempting to load last location...");

  let found = false;
  try {
    const stored = window.localStorage.getItem(LOCAL_STORAGE_KEY);
    if (stored) {
      const parsed = JSON.parse(stored);
      if (parsed?.lat && parsed?.lon) {
        console.log("Last saved location from localStorage:", parsed.lat, parsed.lon);
        animateMarker(parsed.lat, parsed.lon);
        found = true;
      } else {
        console.log("No valid location found in localStorage.");
      }
    } else {
      console.log("No location stored in localStorage.");
    }
  } catch (e) {
    console.warn("Unable to load last location", e);
  }

  db.ref("gps").limitToLast(1).once("value").then((snapshot) => {
    const data = snapshot.val();
    console.log("Raw Firebase gps snapshot:", data);

    const gps = parseGpsSnapshot(data);
    if (!gps) {
      console.log("Firebase gps data did not contain valid lat/lon.");
      if (!found) {
        console.log("Using offline fallback location.");
        animateMarker(FALLBACK_LAT, FALLBACK_LON);
        document.getElementById("coords").innerHTML =
          `📍 Lat: ${FALLBACK_LAT.toFixed(6)} | Lon: ${FALLBACK_LON.toFixed(6)}`;
      }
      if (data && typeof data === 'object') {
        for (const key in data) {
          if (!Object.prototype.hasOwnProperty.call(data, key)) continue;
          console.log("Firebase child entry:", key, data[key]);
        }
      }
      return;
    }

    console.log("Last location fetched from Firebase:", gps.lat, gps.lon, "(key:", gps.key || 'root', ")");
    animateMarker(gps.lat, gps.lon);
    saveLastLocation(gps.lat, gps.lon);
  }).catch((error) => {
    console.warn("Firebase fetch failed", error);
    if (!found) {
      console.log("Using offline fallback location because Firebase fetch failed.");
      animateMarker(FALLBACK_LAT, FALLBACK_LON);
      document.getElementById("coords").innerHTML =
        `📍 Lat: ${FALLBACK_LAT.toFixed(6)} | Lon: ${FALLBACK_LON.toFixed(6)}`;
    }
  });
}

loadLastLocation();

// � Make the panel draggable on touch/mobile
const panel = document.querySelector('.panel');
let panelDrag = false;
let dragOffsetX = 0;
let dragOffsetY = 0;

if (panel) {
  panel.addEventListener('pointerdown', (event) => {
    if (event.target.closest('button')) return;
    panelDrag = true;
    dragOffsetX = event.clientX - panel.offsetLeft;
    dragOffsetY = event.clientY - panel.offsetTop;
    panel.setPointerCapture(event.pointerId);
  });

  panel.addEventListener('pointermove', (event) => {
    if (!panelDrag) return;
    const x = event.clientX - dragOffsetX;
    const y = event.clientY - dragOffsetY;
    const maxX = window.innerWidth - panel.offsetWidth - 10;
    const maxY = window.innerHeight - panel.offsetHeight - 10;

    panel.style.left = Math.min(Math.max(10, x), maxX) + 'px';
    panel.style.top = Math.min(Math.max(10, y), maxY) + 'px';
    panel.style.right = 'auto';
    panel.style.bottom = 'auto';
  });

  panel.addEventListener('pointerup', () => {
    panelDrag = false;
  });
  panel.addEventListener('pointercancel', () => {
    panelDrag = false;
  });
}

// �🔥 FIREBASE LISTENER
db.ref("gps").limitToLast(1).on("value", (snapshot) => {

  const data = snapshot.val();
  if (!data) return;

  const parsed = parseGpsSnapshot(data);
  if (!parsed) {
    console.warn("Realtime Firebase gps update missing lat/lon", data);
    if (data && typeof data === 'object') {
      for (const key in data) {
        if (!Object.prototype.hasOwnProperty.call(data, key)) continue;
        console.log("Realtime child entry:", key, data[key]);
      }
    }
    return;
  }

  const val = parsed.raw;
  let sim = val.sim || "UNKNOWN";
  let net = val.net || "UNKNOWN";
  let gps = val.gps || "WAITING";

  let lat = parsed.lat;
  let lon = parsed.lon;

  if (!lat || !lon) return;

  // ✅ ONLINE detection
  let isNew = false;

  if (lastLat === null || lastLon === null) {
    isNew = true;
  } else if (
    Math.abs(lat - lastLat) > 0.00001 ||
    Math.abs(lon - lastLon) > 0.00001
  ) {
    isNew = true;
  }

  if (isNew) {
    lastUpdate = Date.now();
    lastLat = lat;
    lastLon = lon;
  }

  // 🚍 animate bus
  animateMarker(lat, lon);

  // 📏 Distance
  let dist = 0;
  let now = Date.now();

  if (prevLat != null) {
    dist = getDistance(prevLat, prevLon, lat, lon);
  }

  // ⏱ Time
  let timeDiff = 0;
  if (lastUpdateTime != null) {
    timeDiff = (now - lastUpdateTime) / 1000;
  }

  // 🚀 Speed (safe)
  if (timeDiff > 0.5) {
    speed = (dist / timeDiff) * 3600;
  }

  // 🧠 MOVEMENT LOGIC (combined)
  if (dist > 0.003 && speed > 5) {
    moveScore++;
  } else {
    moveScore--;
  }

  moveScore = Math.max(0, Math.min(moveScore, 5));

  if (moveScore >= 3) {
    busStatus = "MOVING";
  } else {
    busStatus = "STOPPED";
  }

  lastUpdateTime = now;

  // 📍 ROUTE (ignore noise)
  if (tracking && dist > 0.003) {
    route.addLatLng([lat, lon]);
    routePoints.push({ lat, lon });
  }

  // 📊 Distance + Speed UI
  if (prevLat != null) {
    totalDistance += dist;

    document.getElementById("speed").innerHTML =
      "🚀 Speed: " + speed.toFixed(2) + " km/h";
  }

  prevLat = lat;
  prevLon = lon;

  document.getElementById("coords").innerHTML =
    `📍 Lat: ${lat.toFixed(6)} | Lon: ${lon.toFixed(6)}`;

  document.getElementById("distance").innerHTML =
    "📏 Distance: " + totalDistance.toFixed(3) + " km";

  // 🚍 BUS STATUS UI (FIXED LOCATION)
  if (busStatus === "MOVING") {
    document.getElementById("busStatus").innerHTML = "🚍 MOVING";
    document.getElementById("busStatus").style.color = "lime";
  } else {
    document.getElementById("busStatus").innerHTML = "🛑 STOPPED";
    document.getElementById("busStatus").style.color = "red";
  }
 document.getElementById("simStatus").style.color =
  sim === "OK" ? "lime" : "red";

document.getElementById("netStatus").style.color =
  net === "OK" ? "lime" : "red";

document.getElementById("gpsStatus").style.color =
  gps === "RECEIVED" ? "lime" : "orange";

});

// 🔴 OFFLINE CHECK
setInterval(() => {
  if (!lastUpdate) return;

  let diff = (Date.now() - lastUpdate) / 1000;

  if (diff > 15) {
    document.getElementById("statusPanel").innerHTML = "🔴 OFFLINE";
    document.getElementById("statusPanel").style.color = "red";
  } else {
    document.getElementById("statusPanel").innerHTML = "🟢 ONLINE";
    document.getElementById("statusPanel").style.color = "lime";
  }
}, 3000);

// 🚍 ANIMATION (fixed rotation)
function animateMarker(newLat, newLon) {

  if (!marker) {
    currentLatLng = [newLat, newLon];
    marker = L.marker(currentLatLng, {
      icon: busIcon,
      rotationAngle: 0
    }).addTo(map);
    map.setView(currentLatLng, 16);
    return;
  }

  if (!currentLatLng) {
    currentLatLng = [newLat, newLon];
    marker.setLatLng(currentLatLng);
    map.setView(currentLatLng, 16);
    return;
  }

  let steps = 20;
  let i = 0;

  let lat1 = currentLatLng[0];
  let lon1 = currentLatLng[1];

  let dist = getDistance(lat1, lon1, newLat, newLon);

  let latStep = (newLat - lat1) / steps;
  let lonStep = (newLon - lon1) / steps;

  let newAngle = getAngle(lat1, lon1, newLat, newLon);

  let diff = newAngle - lastAngle;
  if (diff > 180) diff -= 360;
  if (diff < -180) diff += 360;

  let angle = lastAngle + diff * 0.3;
  lastAngle = angle;

  if (animationInterval) clearInterval(animationInterval);

  animationInterval = setInterval(() => {

    i++;

    let lat = lat1 + latStep * i;
    let lon = lon1 + lonStep * i;

    marker.setLatLng([lat, lon]);

    if (dist > 0.005) {
      marker.setRotationAngle(angle);
    }

    map.panTo([lat, lon]);

    if (i >= steps) {
      clearInterval(animationInterval);
      currentLatLng = [newLat, newLon];
    }

  }, 50);
}

// 📏 DISTANCE
function getDistance(lat1, lon1, lat2, lon2) {
  let R = 6371;
  let dLat = (lat2 - lat1) * Math.PI / 180;
  let dLon = (lon2 - lon1) * Math.PI / 180;

  let a = Math.sin(dLat / 2) ** 2 +
    Math.cos(lat1 * Math.PI / 180) *
    Math.cos(lat2 * Math.PI / 180) *
    Math.sin(dLon / 2) ** 2;

  return R * (2 * Math.atan2(Math.sqrt(a), Math.sqrt(1 - a)));
}

// 🧭 ANGLE
function getAngle(lat1, lon1, lat2, lon2) {
  return Math.atan2(lon2 - lon1, lat2 - lat1) * (180 / Math.PI);
}

// 🎮 BUTTONS
function startTracking() {
  tracking = true;
  routePoints = [];
  route.setLatLngs([]);
  totalDistance = 0;
  alert("▶️ Tracking Started");
}

function stopTracking() {
  tracking = false;
  alert("⏹ Tracking Stopped");
}

function addStop() {
  const location = currentLatLng || [FALLBACK_LAT, FALLBACK_LON];
  if (!location || location.length !== 2) {
    alert("⚠️ No current location available to pin.");
    return;
  }

  const [lat, lon] = location;
  console.log("addStop clicked", { lat, lon, currentLatLng });

  const stopMarker = L.circleMarker([lat, lon], {
    color: "orange",
    fillColor: "#f39c12",
    fillOpacity: 0.8,
    radius: 8,
    weight: 2
  }).addTo(map);

  stopMarkers.push(stopMarker);

  db.ref("points").push({
    timestamp: new Date().toISOString(),
    lat: Number(lat),
    lon: Number(lon),
    routeLength: routePoints.length,
    message: "Stop added"
  }).then(() => {
    console.log("Point saved to Firebase", { lat, lon });
    alert("📍 Stop pinned and saved to Firebase!");
  }).catch((error) => {
    console.warn("Unable to save stop point", error);
    alert("⚠️ Failed to save stop point. Check console.");
  });
}

function clearRoute() {
  route.setLatLngs([]);
  routePoints = [];
  totalDistance = 0;
  alert("🧹 Route Cleared");
}

function saveRoute() {
  if (routePoints.length === 0) {
    alert("⚠️ No route to save");
    return;
  }

  db.ref("routes").push({
    timestamp: new Date().toString(),
    path: routePoints
  });

  alert("💾 Route Saved!");
}

function loadRoutes() {
  db.ref("routes").once("value", (snapshot) => {
    snapshot.forEach((child) => {
      let path = child.val().path;

      L.polyline(
        path.map(p => [p.lat, p.lon]),
        { color: "red" }
      ).addTo(map);
    });

    alert("📂 Routes Loaded");
  });
}