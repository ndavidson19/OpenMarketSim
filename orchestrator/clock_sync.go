// clock_sync.go
package main

import (
	"context"
	"fmt"
	"log"
	"sort"
	"sync"
	"time"

	pb "github.com/yourusername/financial-sim/proto"
)

// ClockSynchronizer manages clock synchronization across simulation nodes
type ClockSynchronizer struct {
	nodes      []*SimulationNode
	offsets    map[string]time.Duration
	masterNode *SimulationNode
	mutex      sync.RWMutex
}

// NewClockSynchronizer creates a new clock synchronizer
func NewClockSynchronizer(nodes []*SimulationNode) *ClockSynchronizer {
	var masterNode *SimulationNode
	if len(nodes) > 0 {
		masterNode = nodes[0] // Use first node as master by default
	}

	return &ClockSynchronizer{
		nodes:      nodes,
		offsets:    make(map[string]time.Duration),
		masterNode: masterNode,
		mutex:      sync.RWMutex{},
	}
}

// SetMasterNode sets the master clock node
func (cs *ClockSynchronizer) SetMasterNode(node *SimulationNode) {
	cs.mutex.Lock()
	defer cs.mutex.Unlock()
	cs.masterNode = node
}

// GetNodeOffset returns the time offset for a node relative to the master
func (cs *ClockSynchronizer) GetNodeOffset(nodeID string) time.Duration {
	cs.mutex.RLock()
	defer cs.mutex.RUnlock()

	offset, exists := cs.offsets[nodeID]
	if !exists {
		return 0
	}
	return offset
}

// SynchronizeClocks performs clock synchronization across all nodes
func (cs *ClockSynchronizer) SynchronizeClocks(ctx context.Context) error {
	cs.mutex.Lock()
	defer cs.mutex.Unlock()

	if cs.masterNode == nil {
		return fmt.Errorf("no master node set")
	}

	log.Println("Starting clock synchronization...")

	// Get timestamps from all nodes
	type nodeTime struct {
		NodeID        string
		RequestTime   time.Time
		NodeTimestamp int64
		ResponseTime  time.Time
		RoundTrip     time.Duration
	}

	const samplesPerNode = 5
	allSamples := make([]nodeTime, 0, len(cs.nodes)*samplesPerNode)

	// For each node, get multiple timestamp samples
	for _, node := range cs.nodes {
		if node == cs.masterNode {
			continue // Skip master node
		}

		nodeSamples := make([]nodeTime, 0, samplesPerNode)

		for i := 0; i < samplesPerNode; i++ {
			// Record request time
			requestTime := time.Now()

			// Get node's current simulation time
			resp, err := node.Client.GetSimulationState(ctx, &pb.GetStateRequest{})
			if err != nil {
				return fmt.Errorf("failed to get timestamp from node %s: %v", node.ID, err)
			}

			// Record response time
			responseTime := time.Now()

			// Calculate round trip time
			roundTrip := responseTime.Sub(requestTime)

			// Store the sample
			nodeSamples = append(nodeSamples, nodeTime{
				NodeID:        node.ID,
				RequestTime:   requestTime,
				NodeTimestamp: resp.CurrentTimeNs,
				ResponseTime:  responseTime,
				RoundTrip:     roundTrip,
			})

			// Brief pause between samples
			time.Sleep(50 * time.Millisecond)
		}

		// Sort samples by round trip time (lower is better)
		sort.Slice(nodeSamples, func(i, j int) bool {
			return nodeSamples[i].RoundTrip < nodeSamples[j].RoundTrip
		})

		// Keep the best samples (lowest latency)
		bestSamples := nodeSamples
		if len(nodeSamples) > 3 {
			bestSamples = nodeSamples[:3]
		}

		allSamples = append(allSamples, bestSamples...)
	}

	// Get master node timestamp
	masterResp, err := cs.masterNode.Client.GetSimulationState(ctx, &pb.GetStateRequest{})
	if err != nil {
		return fmt.Errorf("failed to get timestamp from master node: %v", err)
	}
	masterTimestamp := masterResp.CurrentTimeNs
	masterTime := time.Now()

	// Calculate offsets relative to master node
	nodeOffsets := make(map[string][]time.Duration)

	for _, sample := range allSamples {
		// Estimate the one-way latency as half the round trip
		oneWayLatency := sample.RoundTrip / 2

		// Estimate the time at which the node's timestamp was valid
		// (midpoint between request and response)
		estimatedNodeSampleTime := sample.RequestTime.Add(oneWayLatency)

		// Calculate time difference between master and node
		masterElapsed := masterTime.Sub(estimatedNodeSampleTime)
		masterSimElapsed := time.Duration(masterTimestamp) - time.Duration(sample.NodeTimestamp)

		// The offset is how much to adjust node's clock to match master
		offset := masterSimElapsed - masterElapsed

		nodeOffsets[sample.NodeID] = append(nodeOffsets[sample.NodeID], offset)
	}

	// Calculate final offsets as median of samples
	for nodeID, offsets := range nodeOffsets {
		if len(offsets) == 0 {
			continue
		}

		// Sort offsets
		sort.Slice(offsets, func(i, j int) bool {
			return offsets[i] < offsets[j]
		})

		// Use median offset
		medianOffset := offsets[len(offsets)/2]
		cs.offsets[nodeID] = medianOffset

		log.Printf("Node %s time offset: %v", nodeID, medianOffset)
	}

	log.Println("Clock synchronization completed")
	return nil
}

// AdjustTimestamp adjusts a timestamp from a specific node to be in sync with the master
func (cs *ClockSynchronizer) AdjustTimestamp(nodeID string, timestamp int64) int64 {
	cs.mutex.RLock()
	defer cs.mutex.RUnlock()

	offset := cs.offsets[nodeID]
	return timestamp + int64(offset)
}

// GetGlobalTime returns the current simulation time in the global reference frame
// This is based on the master node's time
func (cs *ClockSynchronizer) GetGlobalTime(ctx context.Context) (int64, error) {
	cs.mutex.RLock()
	master := cs.masterNode
	cs.mutex.RUnlock()

	if master == nil {
		return 0, fmt.Errorf("no master node set")
	}

	resp, err := master.Client.GetSimulationState(ctx, &pb.GetStateRequest{})
	if err != nil {
		return 0, fmt.Errorf("failed to get time from master node: %v", err)
	}

	return resp.CurrentTimeNs, nil
}

// PeriodicSync performs clock synchronization at regular intervals
func (cs *ClockSynchronizer) PeriodicSync(ctx context.Context, interval time.Duration) {
	ticker := time.NewTicker(interval)
	defer ticker.Stop()

	for {
		select {
		case <-ctx.Done():
			return
		case <-ticker.C:
			if err := cs.SynchronizeClocks(ctx); err != nil {
				log.Printf("Clock synchronization failed: %v", err)
			}
		}
	}
}

// StartPeriodicSync starts periodic clock synchronization in a separate goroutine
func (cs *ClockSynchronizer) StartPeriodicSync(ctx context.Context, interval time.Duration) {
	go cs.PeriodicSync(ctx, interval)
}

// Clock drift compensation

// DriftCompensator estimates and compensates for clock drift
type DriftCompensator struct {
	nodeID         string
	syncHistory    []syncPoint
	driftRate      float64 // nanoseconds per second
	mutex          sync.RWMutex
	maxHistorySize int
}

type syncPoint struct {
	localTime  int64
	masterTime int64
	recordedAt time.Time
	offset     int64
}

// NewDriftCompensator creates a new drift compensator
func NewDriftCompensator(nodeID string) *DriftCompensator {
	return &DriftCompensator{
		nodeID:         nodeID,
		syncHistory:    make([]syncPoint, 0, 100),
		driftRate:      0,
		maxHistorySize: 100,
	}
}

// RecordSyncPoint records a synchronization point
func (dc *DriftCompensator) RecordSyncPoint(localTime, masterTime int64) {
	dc.mutex.Lock()
	defer dc.mutex.Unlock()

	offset := masterTime - localTime
	point := syncPoint{
		localTime:  localTime,
		masterTime: masterTime,
		recordedAt: time.Now(),
		offset:     offset,
	}

	// Add to history
	dc.syncHistory = append(dc.syncHistory, point)

	// Keep history within size limit
	if len(dc.syncHistory) > dc.maxHistorySize {
		dc.syncHistory = dc.syncHistory[1:]
	}

	// Recalculate drift rate if we have enough points
	if len(dc.syncHistory) >= 10 {
		dc.calculateDriftRate()
	}
}

// calculateDriftRate calculates clock drift rate using linear regression
func (dc *DriftCompensator) calculateDriftRate() {
	if len(dc.syncHistory) < 10 {
		return
	}

	// Use linear regression to estimate drift rate
	// y = offset, x = time elapsed
	var sumX, sumY, sumXY, sumXX float64
	var n float64

	firstPoint := dc.syncHistory[0]
	firstRecordedAt := firstPoint.recordedAt.UnixNano()

	for _, point := range dc.syncHistory {
		// x = time elapsed since first point (in seconds)
		timeElapsed := float64(point.recordedAt.UnixNano()-firstRecordedAt) / 1e9
		// y = offset change
		offsetChange := float64(point.offset - firstPoint.offset)

		sumX += timeElapsed
		sumY += offsetChange
		sumXY += timeElapsed * offsetChange
		sumXX += timeElapsed * timeElapsed
		n++
	}

	// Calculate slope of regression line (nanoseconds per second)
	if n > 1 && sumXX > 0 {
		dc.driftRate = (sumXY - (sumX * sumY / n)) / (sumXX - (sumX * sumX / n))
		log.Printf("Node %s drift rate: %.2f ns/s", dc.nodeID, dc.driftRate)
	}
}

// CompensateForDrift adjusts a timestamp to account for clock drift
func (dc *DriftCompensator) CompensateForDrift(timestamp int64, elapsedSecs float64) int64 {
	dc.mutex.RLock()
	defer dc.mutex.RUnlock()

	// Apply drift compensation: timestamp + (drift_rate * elapsed_time)
	driftAdjustment := int64(dc.driftRate * elapsedSecs)
	return timestamp + driftAdjustment
}

// GetLastOffset returns the most recent offset
func (dc *DriftCompensator) GetLastOffset() int64 {
	dc.mutex.RLock()
	defer dc.mutex.RUnlock()

	if len(dc.syncHistory) == 0 {
		return 0
	}
	return dc.syncHistory[len(dc.syncHistory)-1].offset
}

// GetCompensatedOffset returns the current estimated offset with drift compensation
func (dc *DriftCompensator) GetCompensatedOffset() int64 {
	dc.mutex.RLock()
	defer dc.mutex.RUnlock()

	if len(dc.syncHistory) == 0 {
		return 0
	}

	lastPoint := dc.syncHistory[len(dc.syncHistory)-1]
	elapsedSecs := float64(time.Since(lastPoint.recordedAt).Nanoseconds()) / 1e9
	driftAdjustment := int64(dc.driftRate * elapsedSecs)

	return lastPoint.offset + driftAdjustment
}

// Usage example (from main orchestrator)
func setupClockSynchronization(ctx context.Context, orchestrator *Orchestrator) {
	// Create clock synchronizer
	clockSync := NewClockSynchronizer(orchestrator.Nodes)

	// Set the master node (first node)
	if len(orchestrator.Nodes) > 0 {
		clockSync.SetMasterNode(orchestrator.Nodes[0])
	}

	// Perform initial synchronization
	if err := clockSync.SynchronizeClocks(ctx); err != nil {
		log.Printf("Initial clock synchronization failed: %v", err)
	}

	// Start periodic synchronization
	clockSync.StartPeriodicSync(ctx, 10*time.Second)

	// Store for later use
	orchestrator.clockSync = clockSync
}
