#!/bin/bash

echo "=== Manual Test Procedure ==="
echo ""
echo "1. First, start the sensor gateway in one terminal:"
echo "   ./sensor_gateway 5678 5"
echo ""
echo "2. In another terminal, run a sensor node:"
echo "   ./sensor_node 1 15 0.5 127.0.0.1 5678 10"
echo ""
echo "3. Check the output files:"
echo "   cat gateway.log"
echo "   cat sensor_data_recv.txt"
echo ""
echo "Let me run a quick test for you..."

# Clean up first
pkill -f sensor_gateway 2>/dev/null
pkill -f sensor_node 2>/dev/null
rm -f gateway.log sensor_data_recv.txt logFifo 2>/dev/null

echo ""
echo "Starting gateway on port 5678 with 5 second timeout..."
./sensor_gateway 5678 5 &
GATEWAY_PID=$!

echo "Gateway PID: $GATEWAY_PID"
echo "Waiting 2 seconds for gateway to start..."
sleep 2

echo ""
echo "Starting sensor node (room=1, sensor=15, 5 measurements)..."
./sensor_node 1 15 0.5 127.0.0.1 5678 5 &
SENSOR_PID=$!

echo "Sensor PID: $SENSOR_PID"
echo "Waiting for sensor to complete..."
wait $SENSOR_PID 2>/dev/null

echo ""
echo "Waiting for gateway timeout..."
sleep 6

echo ""
echo "Checking if gateway is still running..."
if ps -p $GATEWAY_PID > /dev/null 2>&1; then
    echo "Gateway still running, killing it..."
    kill $GATEWAY_PID 2>/dev/null
    wait $GATEWAY_PID 2>/dev/null
fi

echo ""
echo "=== Test Results ==="
if [ -f "gateway.log" ]; then
    echo "Gateway log created:"
    echo "-------------------"
    cat gateway.log
    echo "-------------------"
else
    echo "ERROR: gateway.log not created!"
fi

echo ""
if [ -f "sensor_data_recv.txt" ]; then
    echo "Sensor data file created with $(wc -l < sensor_data_recv.txt) measurements"
    echo "First 3 lines:"
    head -3 sensor_data_recv.txt
else
    echo "ERROR: sensor_data_recv.txt not created!"
fi

echo ""
echo "=== Debug Information ==="
echo "Files in directory:"
ls -la *.log *.txt 2>/dev/null || echo "No log files found"

echo ""
echo "If tests failed, check:"
echo "1. Make sure port 5678 is not in use"
echo "2. Check room_sensor.map file exists"
echo "3. Verify the gateway starts without errors"