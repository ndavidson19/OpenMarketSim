# Financial Simulation Helm Chart

This Helm chart deploys a distributed financial market simulation system on Kubernetes.

## Prerequisites

- Kubernetes 1.19+
- Helm 3.2.0+
- PV provisioner support in the underlying infrastructure (if persistence is enabled)

## Getting Started

### Add the Repository

```bash
helm repo add financial-sim https://yourusername.github.io/financial-sim/charts
helm repo update
```

### Install the Chart

To install the chart with the release name `my-sim`:

```bash
helm install my-sim financial-sim/financial-sim
```

Or to install from local directory:

```bash
helm install my-sim ./financial-sim
```

### Install with Specific Values

```bash
helm install my-sim financial-sim/financial-sim \
  --set global.namespace=my-namespace \
  --set operator.resources.requests.memory=256Mi \
  --set defaultSimulation.enabled=true
```

## Configuration

The following table lists the configurable parameters of the financial-sim chart and their default values.

### Global Parameters

| Parameter | Description | Default |
| --------- | ----------- | ------- |
| `global.namespace` | Namespace where the simulation resources will be created | `market-sims` |
| `global.imagePullSecrets` | Image pull secrets | `[]` |
| `global.imageTag` | Tag to use for all components | `latest` |

### Operator Parameters

| Parameter | Description | Default |
| --------- | ----------- | ------- |
| `operator.image` | Image to use for the operator | `financial-sim-operator` |
| `operator.replicas` | Replica count for the operator | `1` |
| `operator.resources` | Resource limits for the operator | `{}` |
| `operator.nodeSelector` | Node selector for operator pod | `{}` |
| `operator.tolerations` | Tolerations for operator pod | `[]` |
| `operator.affinity` | Affinity settings for operator pod | `{}` |
| `operator.service.type` | Service type for the operator | `ClusterIP` |
| `operator.service.port` | Service port for the operator | `8080` |

### Node Parameters

| Parameter | Description | Default |
| --------- | ----------- | ------- |
| `node.image` | Image to use for the simulation nodes | `financial-sim` |
| `node.resources` | Resource limits for simulation nodes | `{}` |
| `node.storage.enabled` | Enable persistent storage | `true` |
| `node.storage.storageClass` | Storage class for persistent storage | `standard` |
| `node.storage.size` | Size of persistent volumes | `10Gi` |

### RBAC Parameters

| Parameter | Description | Default |
| --------- | ----------- | ------- |
| `rbac.create` | Create ClusterRole/Role and ServiceAccount | `true` |
| `rbac.serviceAccountName` | Use existing ServiceAccount | `financial-sim-operator` |

### CRD Parameters

| Parameter | Description | Default |
| --------- | ----------- | ------- |
| `crd.install` | Install the CRD | `true` |
| `crd.cleanup` | Cleanup the CRD on uninstall | `false` |

### Default Simulation Parameters

| Parameter | Description | Default |
| --------- | ----------- | ------- |
| `defaultSimulation.enabled` | Create a default simulation | `false` |
| `defaultSimulation.name` | Name of the default simulation | `default-simulation` |
| `defaultSimulation.description` | Description of the default simulation | `Default financial market simulation` |
| `defaultSimulation.cluster.minNodes` | Minimum number of simulation nodes | `1` |
| `defaultSimulation.cluster.maxNodes` | Maximum number of simulation nodes | `3` |

### Other Parameters

| Parameter | Description | Default |
| --------- | ----------- | ------- |
| `zeromq.hwm` | ZeroMQ high water mark | `1000` |
| `zeromq.tcpKeepalive` | Enable TCP keepalive | `true` |
| `logging.level` | Logging level | `info` |
| `logging.format` | Logging format | `json` |

## Creating a Simulation

The chart can automatically create a default simulation if you set `defaultSimulation.enabled=true`. Alternatively, you can create your own simulation by applying a FinancialSimulation custom resource:

```yaml
apiVersion: sim.financial.io/v1
kind: FinancialSimulation
metadata:
  name: my-simulation
  namespace: market-sims
spec:
  name: "My Financial Market Simulation"
  description: "A simple simulation example"
  cluster:
    enabled: true
    minNodes: 1
    nodeImage: "financial-sim:latest"
  simulationParams:
    duration: "1h"
  exchanges:
    - id: "exchange1"
      name: "Primary Exchange"
      type: "equity"
  instruments:
    - id: "SMPL"
      symbol: "SMPL"
      initialPrice: 100.0
  actors:
    - id: "mm1"
      type: "market_maker"
      count: 1
      initialCapital: 1000000.0
  outputConfig:
    outputFormat: "csv"
    metrics: ["price", "volume"]
```

Apply with:

```bash
kubectl apply -f my-simulation.yaml
```

## Uninstalling the Chart

To uninstall/delete the `my-sim` deployment:

```bash
helm delete my-sim
```

## License

Copyright © 2023 Your Name

Licensed under the Apache License, Version 2.0