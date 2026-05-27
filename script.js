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

// 🚍 BUS ICON
var busIcon = L.icon({
  iconUrl: "bus.png",
  iconSize: [40, 40],
  iconAnchor: [14, 14]
});

var marker = L.marker([10, 78], {
  icon: busIcon,
  rotationAngle: 0
}).addTo(map);

// 📍 ROUTE
var route = L.polyline([], { color: 'blue', weight: 4 }).addTo(map);

// 🔄 AUTOMATICALLY LOAD SAVED STOPS FROM FIREBASE ON INITIAL PAGE LOAD
db.ref("stops").once("value", (snapshot) => {
  if (snapshot.exists()) {
    snapshot.forEach((childSnapshot) => {
      const stop = childSnapshot.val();
      createStopMarker(stop.lat, stop.lon, stop.stopName);
    });
  }
});

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
let stopMarkers = [];

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

  // 📡 UPDATE STATUS UI (FIXED)
  document.getElementById("simStatus").innerHTML = "📡 SIM: " + sim;
  document.getElementById("netStatus").innerHTML = "📶 NETWORK: " + net;
  document.getElementById("gpsStatus").innerHTML = "📍 GPS: " + gps;

  document.getElementById("simStatus").style.color =
    sim === "OK" ? "lime" : "red";

  document.getElementById("netStatus").style.color =
    net === "OK" ? "lime" : "red";

  document.getElementById("gpsStatus").style.color =
    gps === "RECEIVED" ? "lime" : "orange";

  // 📏 Distance
  let dist = 0;
  let now = Date.now();

  if (prevLat != null) {
    dist = getDistance(prevLat, prevLon, lat, lon);
  }

  // 🚍 MOVE ONLY IF REAL MOVEMENT
  if (dist > 0.005) {
    animateMarker(lat, lon);
  }

  // ⏱ Time
  let timeDiff = 0;
  if (lastUpdateTime != null) {
    timeDiff = (now - lastUpdateTime) / 1000;
  }

  // 🚀 Speed
  if (timeDiff > 0.5) {
    speed = (dist / timeDiff) * 3600;
  }

  // 🧠 MOVEMENT LOGIC
  if (dist > 0.003 && speed > 5) {
    moveScore++;
  } else {
    moveScore--;
  }

  moveScore = Math.max(0, Math.min(moveScore, 5));

  busStatus = moveScore >= 3 ? "MOVING" : "STOPPED";

  lastUpdateTime = now;

  // 📍 ROUTE
  if (tracking && dist > 0.003) {
    route.addLatLng([lat, lon]);
    routePoints.push({ lat, lon });
  }

  // 📊 UI
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

  // 🚍 BUS STATUS
  if (busStatus === "MOVING") {
    document.getElementById("busStatus").innerHTML = "🚍 MOVING";
    document.getElementById("busStatus").style.color = "lime";
  } else {
    document.getElementById("busStatus").innerHTML = "🛑 STOPPED";
    document.getElementById("busStatus").style.color = "red";
  }

  // 📡 ONLINE STATUS
  lastUpdate = Date.now();
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

// 🚍 ANIMATION
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

    marker.setRotationAngle(angle);

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
// ➕ ADD STOP FUNCTION
function addStop() {
  // Guard clause: Ensure we have actually received data points from the bus first
  if (!prevLat || !prevLon) {
    alert("⚠️ No GPS location available yet to mark a stop!");
    return;
  }

  // Define stop payload structure
  const stopData = {
    lat: prevLat,
    lon: prevLon,
    timestamp: new Date().toString(),
    stopName: "Stop " + (stopMarkers.length + 1)
  };

  // 1. Push payload into a clean "stops" child node in Firebase Real-time Database
  db.ref("stops").push(stopData)
    .then(() => {
      // 2. Drop the visual pin point on the map array interface natively
      createStopMarker(stopData.lat, stopData.lon, stopData.stopName);
      alert(`📌 ${stopData.stopName} Saved Successfully!`);
    })
    .catch((error) => {
      console.error("Firebase Stop Save Failure: ", error);
      alert("❌ Database error. Could not save stop configuration.");
    });
}

// Helper function to create a pin point on the map canvas
function createStopMarker(lat, lon, label) {
  // Use Leaflet's default red/blue pin layout and bind a popup to label it
  let stopMarker = L.marker([lat, lon])
    .addTo(map)
    .bindPopup(`<b>${label}</b><br>Lat: ${lat.toFixed(6)}<br>Lon: ${lon.toFixed(6)}`)
    .openPopup();

  // Store marker instance in our global array context
  stopMarkers.push(stopMarker);
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