import 'dart:async';
import 'dart:math';
import 'package:flutter/material.dart';
import 'package:flutter_map/flutter_map.dart';
import 'package:latlong2/latlong.dart';
import 'package:firebase_core/firebase_core.dart';
import 'package:firebase_database/firebase_database.dart';
import 'firebase_options.dart';

void main() async {
  WidgetsFlutterBinding.ensureInitialized();
  await Firebase.initializeApp(options: DefaultFirebaseOptions.currentPlatform);
  runApp(MyApp());
}

class MyApp extends StatelessWidget {
  @override
  Widget build(BuildContext context) {
    return MaterialApp(debugShowCheckedModeBanner: false, home: MapScreen());
  }
}

class MapScreen extends StatefulWidget {
  @override
  _MapScreenState createState() => _MapScreenState();
}

class _MapScreenState extends State<MapScreen> {
  final DatabaseReference dbRef = FirebaseDatabase.instance.ref("gps");

  final MapController mapController = MapController();
  final Distance distanceCalculator = Distance();

  LatLng currentLocation = LatLng(10.0, 78.0);
  LatLng previousLocation = LatLng(10.0, 78.0);

  List<LatLng> routePoints = [];

  bool isConnected = false;
  String logText = "Waiting for data...";

  Timer? animationTimer;
  double t = 0.0;
  double busAngle = 0.0;

  // 🔥 NEW FEATURES
  double speed = 0.0;
  double totalDistance = 0.0;
  DateTime? lastUpdateTime;

  @override
  void initState() {
    super.initState();

    // ✅ Always get latest Firebase value
    dbRef.limitToLast(1).onValue.listen((event) {
      setState(() {
        isConnected = true;
      });

      if (event.snapshot.value == null) {
        logText = "⚠️ No data";
        return;
      }

      final data = event.snapshot.value as Map;
      final lastEntry = data.values.first;

      if (lastEntry["lat"] == null || lastEntry["lon"] == null) {
        logText = "⚠️ Waiting for GPS fix...";
        return;
      }

      double lat = (lastEntry["lat"] as num).toDouble();
      double lon = (lastEntry["lon"] as num).toDouble();

      LatLng newLocation = LatLng(lat, lon);
      DateTime now = DateTime.now();

      // ✅ FIRST DATA (initialize only)
      if (routePoints.isEmpty) {
        previousLocation = newLocation;
        currentLocation = newLocation;
        if (routePoints.isEmpty ||
    distanceCalculator(routePoints.last, newLocation) > 5) {
  routePoints.add(newLocation);
}
        lastUpdateTime = now;

        setState(() {
          logText = "📍 Lat: $lat | Lon: $lon";
        });

        mapController.move(newLocation, 18);
        return;
      }

      // ✅ DISTANCE
      double distMeters = distanceCalculator(previousLocation, newLocation);

      // 🚫 Ignore GPS noise (very important)
      if (distMeters < 2) return;

      // ✅ TIME
      double timeSeconds =
          now.difference(lastUpdateTime!).inMilliseconds / 1000.0;

      if (timeSeconds > 0) {
        speed = (distMeters / timeSeconds) * 3.6;
      }

      totalDistance += distMeters / 1000.0;

      // ✅ UPDATE STATE
      previousLocation = newLocation;
      lastUpdateTime = now;
      routePoints.add(newLocation);

      setState(() {
        logText = "📍 Lat: $lat | Lon: $lon";
      });

      // ✅ AUTO FOLLOW
      mapController.move(newLocation, 17);

      animateMovement(newLocation);
    });
  }

  void animateMovement(LatLng newLocation) {
    animationTimer?.cancel();
    t = 0.0;

    animationTimer = Timer.periodic(Duration(milliseconds: 30), (timer) {
      t += 0.02;

      if (t >= 1.0) {
        t = 1.0;
        timer.cancel();
      }

      double lat =
          previousLocation.latitude +
          (newLocation.latitude - previousLocation.latitude) * t;

      double lon =
          previousLocation.longitude +
          (newLocation.longitude - previousLocation.longitude) * t;

      busAngle = atan2(
        newLocation.longitude - previousLocation.longitude,
        newLocation.latitude - previousLocation.latitude,
      );

      setState(() {
        currentLocation = LatLng(lat, lon);
      });
    });
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: Text("🚍 Smart Bus Tracker"),
        backgroundColor: Colors.green,
      ),
      body: Stack(
        children: [
          /// 🗺 MAP
          FlutterMap(
            mapController: mapController,
            options: MapOptions(
              initialCenter: currentLocation,
              initialZoom: 15,
            ),
            children: [
              TileLayer(
                urlTemplate:
                    "https://{s}.basemaps.cartocdn.com/light_all/{z}/{x}/{y}.png",
                subdomains: ['a', 'b', 'c'],
                userAgentPackageName: 'com.example.bus_tracking',
              ),

              /// ROUTE
              PolylineLayer(
                polylines: [
                  Polyline(
                    points: routePoints,
                    strokeWidth: 4,
                    color: Colors.blue,
                  ),
                ],
              ),

              /// BUS
              MarkerLayer(
                markers: [
                  Marker(
                    point: currentLocation,
                    width: 50,
                    height: 50,
                    child: Transform.rotate(
                      angle: busAngle,
                      child: Icon(
                        Icons.directions_bus,
                        size: 40,
                        color: Colors.red,
                      ),
                    ),
                  ),
                ],
              ),
            ],
          ),

          /// STATUS
          Positioned(
            top: 10,
            right: 10,
            child: Container(
              padding: EdgeInsets.all(8),
              decoration: BoxDecoration(
                color: isConnected ? Colors.green : Colors.red,
                borderRadius: BorderRadius.circular(10),
              ),
              child: Text(
                isConnected ? "ONLINE" : "OFFLINE",
                style: TextStyle(color: Colors.white),
              ),
            ),
          ),

          /// 📊 DATA PANEL
          Positioned(
            bottom: 20,
            left: 20,
            right: 20,
            child: Container(
              padding: EdgeInsets.all(12),
              decoration: BoxDecoration(
                color: Colors.black87,
                borderRadius: BorderRadius.circular(12),
              ),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text(
                    "📡 Firebase: ${isConnected ? "ONLINE" : "OFFLINE"}",
                    style: TextStyle(color: Colors.green),
                  ),
                  SizedBox(height: 5),
                  Text(logText, style: TextStyle(color: Colors.white)),
                  SizedBox(height: 5),
                  Text(
                    "🚀 Speed: ${speed.toStringAsFixed(2)} km/h",
                    style: TextStyle(color: Colors.orange),
                  ),
                  Text(
                    "📍 Distance: ${totalDistance.toStringAsFixed(3)} km",
                    style: TextStyle(color: Colors.blue),
                  ),
                ],
              ),
            ),
          ),
        ],
      ),
    );
  }
}
