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
  StreamSubscription? gpsSub;

  final MapController mapController = MapController();
  final Distance distanceCalculator = Distance();

  LatLng currentLocation = LatLng(10.0, 78.0);
  LatLng previousLocation = LatLng(10.0, 78.0);

  List<LatLng> routePoints = [];

  bool isConnected = false;
  String logText = "Waiting for data...";
  String busStatus = "STOPPED";
  bool isTracking = false;
  List<LatLng> recordedRoute = [];
  Timer? animationTimer;
  double t = 0.0;
  double busAngle = 0.0;
  double? lastLatReceived;
  double? lastLonReceived;

  // 🔥 NEW FEATURES
  double speed = 0.0;
  double totalDistance = 0.0;
  DateTime? lastUpdateTime;
  DateTime? lastFirebaseUpdate;

  @override
  void initState() {
    super.initState();

    // ✅ Always get latest Firebase value
    gpsSub = dbRef.limitToLast(1).onValue.listen((event) {
     ;

         Timer.periodic(Duration(seconds: 7), (timer) {
        if (lastFirebaseUpdate == null) return;

        final diff = DateTime.now().difference(lastFirebaseUpdate!).inSeconds;

        if (diff > 15) {
          setState(() {
            isConnected = false;
          });
        } else {
          setState(() {
            isConnected = true;
          });
        }
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

      bool isNewData = false;

      if (lastLatReceived == null || lastLonReceived == null) {
        isNewData = true;
      } else {
        if ((lat - lastLatReceived!).abs() > 0.00001 ||
            (lon - lastLonReceived!).abs() > 0.00001) {
          isNewData = true;
        }
      }
      if (isNewData) {
        lastFirebaseUpdate = DateTime.now();
        lastLatReceived = lat;
        lastLonReceived = lon;

        setState(() {
          isConnected = true;
        });
      }

      LatLng newLocation = LatLng(lat, lon);
      DateTime now = DateTime.now();

      // ✅ FIRST DATA (initialize only)
      if (routePoints.isEmpty) {
        previousLocation = newLocation;
        currentLocation = newLocation;

        routePoints.add(newLocation); // ✅ ALWAYS add first point

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
      if (isTracking) {
        routePoints.add(newLocation);
        recordedRoute.add(newLocation);
      }

      if (routePoints.length > 500) {
        routePoints.removeAt(0);
      }

      setState(() {
        logText = "📍 Lat: $lat | Lon: $lon";
        if (speed > 10) {
          busStatus = "MOVING";
        } else {
          busStatus = "STOPPED";
        }
      });

      // ✅ AUTO FOLLOW
      mapController.move(newLocation, 17);

      animateMovement(newLocation);
   
    });
  }

  Future<void> saveRouteToFirebase() async {
    if (recordedRoute.isEmpty) {
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(
          content: Text("⚠️ No route to save"),
          backgroundColor: Colors.orange,
        ),
      );
      return;
    }

    List<Map<String, double>> routeData = recordedRoute.map((point) {
      return {"lat": point.latitude, "lon": point.longitude};
    }).toList();

    await FirebaseDatabase.instance.ref("routes").push().set({
      "timestamp": DateTime.now().toString(),
      "path": routeData,
    });

    ScaffoldMessenger.of(context).showSnackBar(
      SnackBar(
        content: Text("💾 Route saved successfully"),
        backgroundColor: Colors.green,
      ),
    );
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
        newLocation.latitude - previousLocation.latitude,
        newLocation.longitude - previousLocation.longitude,
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
        title: Text("Bus Tracker"),
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
            bottom: 180,
            left: 20,
            right: 20,
            child: Row(
              mainAxisAlignment: MainAxisAlignment.spaceEvenly,
              children: [
                // ▶️ START
                ElevatedButton(
                  style: ElevatedButton.styleFrom(
                    backgroundColor: Colors.green,
                  ),
                  onPressed: () {
                    setState(() {
                      isTracking = true;
                      routePoints.clear();
                      recordedRoute.clear();
                    });

                    ScaffoldMessenger.of(context).showSnackBar(
                      SnackBar(
                        content: Text("▶️ Tracking Started"),
                        backgroundColor: Colors.green,
                      ),
                    );
                  },
                  child: Text("START"),
                ),

                // ⏹ STOP
                ElevatedButton(
                  style: ElevatedButton.styleFrom(backgroundColor: Colors.red),
                  onPressed: () {
                    setState(() {
                      isTracking = false;
                    });

                    ScaffoldMessenger.of(context).showSnackBar(
                      SnackBar(
                        content: Text("⏹ Tracking Stopped"),
                        backgroundColor: Colors.red,
                      ),
                    );
                  },
                  child: Text("STOP"),
                ),

                // 💾 SAVE
                ElevatedButton(
                  style: ElevatedButton.styleFrom(backgroundColor: Colors.blue),
                  onPressed: () async {
                    await saveRouteToFirebase();
                  },
                  child: Text("SAVE"),
                ),
              ],
            ),
          ),
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
                  Text(
                    busStatus == "MOVING" ? "🟢 Bus Moving" : "🔴 Bus Stopped",
                    style: TextStyle(
                      color: busStatus == "MOVING" ? Colors.green : Colors.red,
                      fontWeight: FontWeight.bold,
                      fontSize: 16,
                    ),
                  ),
                ],
              ),
            ),
          ),
        ],
      ),
    );
  }

  @override
  void dispose() {
    gpsSub?.cancel(); // 🔥 stop Firebase listener
    animationTimer?.cancel(); // 🔥 stop animation
    super.dispose();
  }
}
