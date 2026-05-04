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

firebase.initializeApp(firebaseConfig);
const db = firebase.database();

// 🗺 MAP
var map = L.map('map').setView([10.0, 78.0], 16);

L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png', {
  attribution: "© OpenStreetMap"
}).addTo(map);

// 🚍 BUS ICON (small = professional look)
var busIcon = L.icon({
  iconUrl: "bus.png",
  iconSize: [28, 28],
  iconAnchor: [14, 14]
});

var marker = L.marker([10, 78], {
  icon: busIcon,
  rotationAngle: 0
}).addTo(map);

// 📍 ROUTE
var route = L.polyline([], { color: 'blue', weight: 4 }).addTo(map);

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

// 🔥 FIREBASE LISTENER
db.ref("gps").limitToLast(1).on("value", (snapshot) => {

  const data = snapshot.val();
  if (!data) return;

  const key = Object.keys(data)[0];
  const val = data[key];
  let sim = val.sim || "UNKNOWN";
let net = val.net || "UNKNOWN";
let gps = val.gps || "WAITING";

  let lat = val.lat;
  let lon = val.lon;

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

  if (!currentLatLng) {
    currentLatLng = [newLat, newLon];
    marker.setLatLng(currentLatLng);
    map.setView(currentLatLng);
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