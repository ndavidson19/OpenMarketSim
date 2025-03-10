// kubernetes_controller.go
package main

import (
	"context"
	"fmt"
	"log"
	"time"

	appsv1 "k8s.io/api/apps/v1"
	corev1 "k8s.io/api/core/v1"
	"k8s.io/apimachinery/pkg/api/errors"
	"k8s.io/apimachinery/pkg/api/resource"
	metav1 "k8s.io/apimachinery/pkg/apis/meta/v1"
	"k8s.io/apimachinery/pkg/runtime"
	"k8s.io/apimachinery/pkg/types"
	"k8s.io/apimachinery/pkg/util/intstr"
	"k8s.io/client-go/kubernetes"
	"k8s.io/client-go/rest"
	"k8s.io/client-go/tools/cache"
	"k8s.io/client-go/tools/clientcmd"
	"k8s.io/client-go/util/workqueue"
	ctrl "sigs.k8s.io/controller-runtime"
	"sigs.k8s.io/controller-runtime/pkg/client"
	"sigs.k8s.io/controller-runtime/pkg/manager"
)

// SimulationController reconciles FinancialSimulation objects
type SimulationController struct {
	client.Client
	Scheme        *runtime.Scheme
	ClientSet     *kubernetes.Clientset
	MessageBroker *MessageBroker
	ConfigService *ConfigService
}

// NewSimulationController creates a new controller
func NewSimulationController(mgr manager.Manager, messageBroker *MessageBroker, configService *ConfigService) (*SimulationController, error) {
	// Create Kubernetes clientset
	config, err := rest.InClusterConfig()
	if err != nil {
		// Fallback to kubeconfig
		kubeconfig := ctrl.GetConfigOrDie()
		config = kubeconfig
	}

	clientset, err := kubernetes.NewForConfig(config)
	if err != nil {
		return nil, fmt.Errorf("failed to create Kubernetes clientset: %v", err)
	}

	return &SimulationController{
		Client:        mgr.GetClient(),
		Scheme:        mgr.GetScheme(),
		ClientSet:     clientset,
		MessageBroker: messageBroker,
		ConfigService: configService,
	}, nil
}

// SetupWithManager sets up the controller with the Manager
func (r *SimulationController) SetupWithManager(mgr ctrl.Manager) error {
	return ctrl.NewControllerManagedBy(mgr).
		For(&FinancialSimulation{}).
		Owns(&appsv1.StatefulSet{}).
		Owns(&corev1.ConfigMap{}).
		Owns(&corev1.Service{}).
		Complete(r)
}

// Reconcile handles the reconciliation loop for FinancialSimulation resources
func (r *SimulationController) Reconcile(ctx context.Context, req ctrl.Request) (ctrl.Result, error) {
	log.Printf("Reconciling FinancialSimulation %s", req.NamespacedName)

	// Fetch the FinancialSimulation instance
	simulation := &FinancialSimulation{}
	err := r.Get(ctx, req.NamespacedName, simulation)
	if err != nil {
		if errors.IsNotFound(err) {
			// Request object not found, could have been deleted
			log.Printf("FinancialSimulation resource not found. Ignoring since object must be deleted")
			return ctrl.Result{}, nil
		}
		// Error reading the object
		log.Printf("Failed to get FinancialSimulation: %v", err)
		return ctrl.Result{}, err
	}

	// Check if the simulation is marked for deletion
	if !simulation.ObjectMeta.DeletionTimestamp.IsZero() {
		// The object is being deleted
		return r.handleDeletion(ctx, simulation)
	}

	// Update simulation status if needed
	if simulation.Status.Phase == "" {
		simulation.Status.Phase = "Pending"
		simulation.Status.NodesRunning = 0
		now := metav1.Now()
		simulation.Status.StartTime = &now

		if err := r.Status().Update(ctx, simulation); err != nil {
			log.Printf("Failed to update simulation status: %v", err)
			return ctrl.Result{}, err
		}
	}

	// Process the simulation based on its phase
	switch simulation.Status.Phase {
	case "Pending":
		return r.handlePendingPhase(ctx, simulation)
	case "Running":
		return r.handleRunningPhase(ctx, simulation)
	case "Completed":
		return r.handleCompletedPhase(ctx, simulation)
	case "Failed":
		return r.handleFailedPhase(ctx, simulation)
	default:
		// Unknown phase
		log.Printf("Unknown simulation phase: %s", simulation.Status.Phase)
		return ctrl.Result{}, nil
	}
}

// handlePendingPhase handles the Pending phase of the simulation
func (r *SimulationController) handlePendingPhase(ctx context.Context, simulation *FinancialSimulation) (ctrl.Result, error) {
	log.Printf("Handling Pending phase for %s", simulation.Name)

	// Create configmap for simulation configuration
	if err := r.createConfigMap(ctx, simulation); err != nil {
		log.Printf("Failed to create ConfigMap: %v", err)

		// Update status with failure condition
		r.updateCondition(ctx, simulation, "ConfigMapCreation", "False", "ConfigMapCreationFailed", err.Error())
		return ctrl.Result{}, err
	}

	// Create services
	if err := r.createServices(ctx, simulation); err != nil {
		log.Printf("Failed to create Services: %v", err)

		// Update status with failure condition
		r.updateCondition(ctx, simulation, "ServicesCreation", "False", "ServicesCreationFailed", err.Error())
		return ctrl.Result{}, err
	}

	// Create node statefulset
	if err := r.createStatefulSet(ctx, simulation); err != nil {
		log.Printf("Failed to create StatefulSet: %v", err)

		// Update status with failure condition
		r.updateCondition(ctx, simulation, "StatefulSetCreation", "False", "StatefulSetCreationFailed", err.Error())
		return ctrl.Result{}, err
	}

	// Update status to Running
	simulation.Status.Phase = "Running"
	r.updateCondition(ctx, simulation, "Initialized", "True", "SimulationInitialized", "Simulation resources created successfully")

	if err := r.Status().Update(ctx, simulation); err != nil {
		log.Printf("Failed to update simulation status: %v", err)
		return ctrl.Result{}, err
	}

	// Requeue to check status after short delay
	return ctrl.Result{RequeueAfter: 5 * time.Second}, nil
}

// handleRunningPhase handles the Running phase of the simulation
func (r *SimulationController) handleRunningPhase(ctx context.Context, simulation *FinancialSimulation) (ctrl.Result, error) {
	log.Printf("Handling Running phase for %s", simulation.Name)

	// Check if the simulation has reached its end condition
	durationStr := simulation.Spec.SimulationParams.Duration
	duration, err := time.ParseDuration(durationStr)
	if err != nil {
		log.Printf("Invalid duration format: %v", err)
		return ctrl.Result{}, err
	}

	startTime := simulation.Status.StartTime.Time
	if time.Since(startTime) > duration {
		// Simulation has completed its duration
		log.Printf("Simulation %s has completed its duration", simulation.Name)

		// Update status to Completed
		simulation.Status.Phase = "Completed"
		now := metav1.Now()
		simulation.Status.CompletionTime = &now
		r.updateCondition(ctx, simulation, "Completed", "True", "SimulationCompleted", "Simulation completed successfully")

		if err := r.Status().Update(ctx, simulation); err != nil {
			log.Printf("Failed to update simulation status: %v", err)
			return ctrl.Result{}, err
		}

		return ctrl.Result{}, nil
	}

	// Check the status of the StatefulSet
	statefulSet := &appsv1.StatefulSet{}
	statefulSetName := fmt.Sprintf("%s-nodes", simulation.Name)
	err = r.Get(ctx, types.NamespacedName{
		Name:      statefulSetName,
		Namespace: simulation.Namespace,
	}, statefulSet)

	if err != nil {
		if errors.IsNotFound(err) {
			log.Printf("StatefulSet %s not found. Recreating...", statefulSetName)
			return r.handlePendingPhase(ctx, simulation)
		}
		log.Printf("Failed to get StatefulSet: %v", err)
		return ctrl.Result{}, err
	}

	// Update node count in status
	nodesRunning := statefulSet.Status.ReadyReplicas
	if simulation.Status.NodesRunning != nodesRunning {
		simulation.Status.NodesRunning = nodesRunning
		if err := r.Status().Update(ctx, simulation); err != nil {
			log.Printf("Failed to update simulation status: %v", err)
			return ctrl.Result{}, err
		}
	}

	// If no nodes are running, mark as failed
	if nodesRunning == 0 && time.Since(startTime) > (5*time.Minute) {
		log.Printf("Simulation %s has no running nodes after 5 minutes", simulation.Name)

		// Update status to Failed
		simulation.Status.Phase = "Failed"
		r.updateCondition(ctx, simulation, "NodesFailed", "True", "NoNodesRunning", "No simulation nodes running after timeout")

		if err := r.Status().Update(ctx, simulation); err != nil {
			log.Printf("Failed to update simulation status: %v", err)
			return ctrl.Result{}, err
		}

		return ctrl.Result{}, nil
	}

	// Requeue to check status periodically
	return ctrl.Result{RequeueAfter: 30 * time.Second}, nil
}

// handleCompletedPhase handles the Completed phase of the simulation
func (r *SimulationController) handleCompletedPhase(ctx context.Context, simulation *FinancialSimulation) (ctrl.Result, error) {
	log.Printf("Handling Completed phase for %s", simulation.Name)

	// Check if we need to clean up resources
	if simulation.Spec.Cluster.PersistentStorage {
		// Keep resources for data analysis
		return ctrl.Result{}, nil
	}

	// Clean up resources but keep the StatefulSet for data access
	// In a real implementation, you might want to reduce replicas to 0 or archive data

	return ctrl.Result{}, nil
}

// handleFailedPhase handles the Failed phase of the simulation
func (r *SimulationController) handleFailedPhase(ctx context.Context, simulation *FinancialSimulation) (ctrl.Result, error) {
	log.Printf("Handling Failed phase for %s", simulation.Name)

	// Could implement recovery mechanisms here

	return ctrl.Result{}, nil
}

// handleDeletion handles the deletion of a FinancialSimulation resource
func (r *SimulationController) handleDeletion(ctx context.Context, simulation *FinancialSimulation) (ctrl.Result, error) {
	log.Printf("Handling deletion of %s", simulation.Name)

	// Clean up all owned resources
	// StatefulSets, Services, ConfigMaps will be garbage collected by Kubernetes
	// due to owner references

	// Mark status as Terminating if not already
	if simulation.Status.Phase != "Terminating" {
		simulation.Status.Phase = "Terminating"
		if err := r.Status().Update(ctx, simulation); err != nil {
			log.Printf("Failed to update simulation status to Terminating: %v", err)
			return ctrl.Result{}, err
		}
	}

	// Check if there are any remaining resources that need manual cleanup
	// (not managed by owner references)

	return ctrl.Result{}, nil
}

// updateCondition updates a condition in the simulation status
func (r *SimulationController) updateCondition(ctx context.Context, simulation *FinancialSimulation,
	conditionType string, status string, reason string, message string) {

	now := metav1.Now()

	// Find existing condition or create new one
	found := false
	for i, condition := range simulation.Status.Conditions {
		if condition.Type == conditionType {
			found = true
			if condition.Status != status || condition.Reason != reason || condition.Message != message {
				simulation.Status.Conditions[i].Status = status
				simulation.Status.Conditions[i].LastTransitionTime = now
				simulation.Status.Conditions[i].Reason = reason
				simulation.Status.Conditions[i].Message = message
			}
			break
		}
	}

	if !found {
		simulation.Status.Conditions = append(simulation.Status.Conditions, Condition{
			Type:               conditionType,
			Status:             status,
			LastTransitionTime: now,
			Reason:             reason,
			Message:            message,
		})
	}
}

// createConfigMap creates a ConfigMap containing the simulation configuration
func (r *SimulationController) createConfigMap(ctx context.Context, simulation *FinancialSimulation) error {
	// Convert config to YAML
	converter := NewConfigConverter()
	configYAML, err := converter.ConvertToYAML(&simulation.Spec)
	if err != nil {
		return fmt.Errorf("failed to convert config to YAML: %v", err)
	}

	// Create ConfigMap
	configMap := &corev1.ConfigMap{
		ObjectMeta: metav1.ObjectMeta{
			Name:      fmt.Sprintf("%s-config", simulation.Name),
			Namespace: simulation.Namespace,
			OwnerReferences: []metav1.OwnerReference{
				{
					APIVersion: simulation.APIVersion,
					Kind:       simulation.Kind,
					Name:       simulation.Name,
					UID:        simulation.UID,
					Controller: func() *bool { b := true; return &b }(),
				},
			},
		},
		Data: map[string]string{
			"simulation-config.yaml": string(configYAML),
		},
	}

	// Check if ConfigMap already exists
	existingConfigMap := &corev1.ConfigMap{}
	err = r.Get(ctx, types.NamespacedName{
		Name:      configMap.Name,
		Namespace: configMap.Namespace,
	}, existingConfigMap)

	if err != nil {
		if errors.IsNotFound(err) {
			// Create new ConfigMap
			if err = r.Create(ctx, configMap); err != nil {
				return fmt.Errorf("failed to create ConfigMap: %v", err)
			}
			log.Printf("Created ConfigMap %s", configMap.Name)
		} else {
			return fmt.Errorf("failed to check existing ConfigMap: %v", err)
		}
	} else {
		// Update existing ConfigMap
		existingConfigMap.Data = configMap.Data
		if err = r.Update(ctx, existingConfigMap); err != nil {
			return fmt.Errorf("failed to update ConfigMap: %v", err)
		}
		log.Printf("Updated ConfigMap %s", configMap.Name)
	}

	return nil
}

// createServices creates the needed services for the simulation
func (r *SimulationController) createServices(ctx context.Context, simulation *FinancialSimulation) error {
	// Create headless service for StatefulSet
	headlessService := &corev1.Service{
		ObjectMeta: metav1.ObjectMeta{
			Name:      fmt.Sprintf("%s-nodes", simulation.Name),
			Namespace: simulation.Namespace,
			OwnerReferences: []metav1.OwnerReference{
				{
					APIVersion: simulation.APIVersion,
					Kind:       simulation.Kind,
					Name:       simulation.Name,
					UID:        simulation.UID,
					Controller: func() *bool { b := true; return &b }(),
				},
			},
		},
		Spec: corev1.ServiceSpec{
			Selector: map[string]string{
				"app": fmt.Sprintf("%s-node", simulation.Name),
			},
			ClusterIP: "None", // Headless service
			Ports: []corev1.ServicePort{
				{
					Name:       "grpc",
					Port:       50051,
					TargetPort: intstr.FromInt(50051),
				},
				{
					Name:       "zeromq-pub",
					Port:       5555,
					TargetPort: intstr.FromInt(5555),
				},
				{
					Name:       "zeromq-router",
					Port:       5556,
					TargetPort: intstr.FromInt(5556),
				},
				{
					Name:       "metrics",
					Port:       9090,
					TargetPort: intstr.FromInt(9090),
				},
				{
					Name:       "http",
					Port:       8080,
					TargetPort: intstr.FromInt(8080),
				},
			},
		},
	}

	// Check if service already exists
	existingService := &corev1.Service{}
	err := r.Get(ctx, types.NamespacedName{
		Name:      headlessService.Name,
		Namespace: headlessService.Namespace,
	}, existingService)

	if err != nil {
		if errors.IsNotFound(err) {
			// Create new service
			if err = r.Create(ctx, headlessService); err != nil {
				return fmt.Errorf("failed to create headless service: %v", err)
			}
			log.Printf("Created headless service %s", headlessService.Name)
		} else {
			return fmt.Errorf("failed to check existing service: %v", err)
		}
	}

	// Create client-facing service for accessing the simulation
	clientService := &corev1.Service{
		ObjectMeta: metav1.ObjectMeta{
			Name:      fmt.Sprintf("%s-api", simulation.Name),
			Namespace: simulation.Namespace,
			OwnerReferences: []metav1.OwnerReference{
				{
					APIVersion: simulation.APIVersion,
					Kind:       simulation.Kind,
					Name:       simulation.Name,
					UID:        simulation.UID,
					Controller: func() *bool { b := true; return &b }(),
				},
			},
		},
		Spec: corev1.ServiceSpec{
			Selector: map[string]string{
				"app":  fmt.Sprintf("%s-node", simulation.Name),
				"role": "orchestrator",
			},
			Type: corev1.ServiceTypeClusterIP,
			Ports: []corev1.ServicePort{
				{
					Name:       "http",
					Port:       80,
					TargetPort: intstr.FromInt(8080),
				},
				{
					Name:       "ws",
					Port:       8081,
					TargetPort: intstr.FromInt(8081),
				},
				{
					Name:       "metrics",
					Port:       9090,
					TargetPort: intstr.FromInt(9090),
				},
			},
		},
	}

	// Check if service already exists
	existingClientService := &corev1.Service{}
	err = r.Get(ctx, types.NamespacedName{
		Name:      clientService.Name,
		Namespace: clientService.Namespace,
	}, existingClientService)

	if err != nil {
		if errors.IsNotFound(err) {
			// Create new service
			if err = r.Create(ctx, clientService); err != nil {
				return fmt.Errorf("failed to create client service: %v", err)
			}
			log.Printf("Created client service %s", clientService.Name)
		} else {
			return fmt.Errorf("failed to check existing client service: %v", err)
		}
	}

	return nil
}

// createStatefulSet creates the StatefulSet for simulation nodes
func (r *SimulationController) createStatefulSet(ctx context.Context, simulation *FinancialSimulation) error {
	// Determine number of replicas
	replicas := int32(simulation.Spec.Cluster.MinNodes)

	// Define resource requirements
	resources := corev1.ResourceRequirements{
		Requests: corev1.ResourceList{
			corev1.ResourceCPU:    resource.MustParse(simulation.Spec.Cluster.Resources.RequestsCPU),
			corev1.ResourceMemory: resource.MustParse(simulation.Spec.Cluster.Resources.RequestsMemory),
		},
		Limits: corev1.ResourceList{
			corev1.ResourceCPU:    resource.MustParse(simulation.Spec.Cluster.Resources.LimitsCPU),
			corev1.ResourceMemory: resource.MustParse(simulation.Spec.Cluster.Resources.LimitsMemory),
		},
	}

	// Create the StatefulSet
	statefulSet := &appsv1.StatefulSet{
		ObjectMeta: metav1.ObjectMeta{
			Name:      fmt.Sprintf("%s-nodes", simulation.Name),
			Namespace: simulation.Namespace,
			OwnerReferences: []metav1.OwnerReference{
				{
					APIVersion: simulation.APIVersion,
					Kind:       simulation.Kind,
					Name:       simulation.Name,
					UID:        simulation.UID,
					Controller: func() *bool { b := true; return &b }(),
				},
			},
		},
		Spec: appsv1.StatefulSetSpec{
			Replicas: &replicas,
			Selector: &metav1.LabelSelector{
				MatchLabels: map[string]string{
					"app": fmt.Sprintf("%s-node", simulation.Name),
				},
			},
			ServiceName: fmt.Sprintf("%s-nodes", simulation.Name),
			Template: corev1.PodTemplateSpec{
				ObjectMeta: metav1.ObjectMeta{
					Labels: map[string]string{
						"app":        fmt.Sprintf("%s-node", simulation.Name),
						"simulation": simulation.Name,
					},
				},
				Spec: corev1.PodSpec{
					ServiceAccountName: simulation.Spec.Cluster.ServiceAccountName,
					Containers: []corev1.Container{
						{
							Name:  "simulation-node",
							Image: simulation.Spec.Cluster.NodeImage,
							Ports: []corev1.ContainerPort{
								{
									Name:          "grpc",
									ContainerPort: 50051,
									Protocol:      corev1.ProtocolTCP,
								},
								{
									Name:          "zeromq-pub",
									ContainerPort: 5555,
									Protocol:      corev1.ProtocolTCP,
								},
								{
									Name:          "zeromq-router",
									ContainerPort: 5556,
									Protocol:      corev1.ProtocolTCP,
								},
								{
									Name:          "metrics",
									ContainerPort: 9090,
									Protocol:      corev1.ProtocolTCP,
								},
								{
									Name:          "http",
									ContainerPort: 8080,
									Protocol:      corev1.ProtocolTCP,
								},
								{
									Name:          "ws",
									ContainerPort: 8081,
									Protocol:      corev1.ProtocolTCP,
								},
							},
							Env: []corev1.EnvVar{
								{
									Name: "POD_NAME",
									ValueFrom: &corev1.EnvVarSource{
										FieldRef: &corev1.ObjectFieldSelector{
											FieldPath: "metadata.name",
										},
									},
								},
								{
									Name: "POD_IP",
									ValueFrom: &corev1.EnvVarSource{
										FieldRef: &corev1.ObjectFieldSelector{
											FieldPath: "status.podIP",
										},
									},
								},
								{
									Name:  "SIMULATION_NAME",
									Value: simulation.Name,
								},
								{
									Name:  "SIMULATION_NAMESPACE",
									Value: simulation.Namespace,
								},
								{
									Name:  "CONFIG_FILE",
									Value: "/etc/simulation/simulation-config.yaml",
								},
								{
									Name:  "OUTPUT_DIR",
									Value: "/data",
								},
								{
									Name:  "ZEROMQ_PUB_PORT",
									Value: "5555",
								},
								{
									Name:  "ZEROMQ_ROUTER_PORT",
									Value: "5556",
								},
								{
									Name:  "METRICS_PORT",
									Value: "9090",
								},
								{
									Name:  "API_PORT",
									Value: "8080",
								},
								{
									Name:  "WS_PORT",
									Value: "8081",
								},
							},
							Resources: resources,
							VolumeMounts: []corev1.VolumeMount{
								{
									Name:      "config-volume",
									MountPath: "/etc/simulation",
								},
								{
									Name:      "data",
									MountPath: "/data",
								},
							},
							LivenessProbe: &corev1.Probe{
								ProbeHandler: corev1.ProbeHandler{
									HTTPGet: &corev1.HTTPGetAction{
										Path: "/health",
										Port: intstr.FromInt(8080),
									},
								},
								InitialDelaySeconds: 30,
								PeriodSeconds:       10,
							},
							ReadinessProbe: &corev1.Probe{
								ProbeHandler: corev1.ProbeHandler{
									HTTPGet: &corev1.HTTPGetAction{
										Path: "/ready",
										Port: intstr.FromInt(8080),
									},
								},
								InitialDelaySeconds: 5,
								PeriodSeconds:       5,
							},
						},
					},
					Volumes: []corev1.Volume{
						{
							Name: "config-volume",
							VolumeSource: corev1.VolumeSource{
								ConfigMap: &corev1.ConfigMapVolumeSource{
									LocalObjectReference: corev1.LocalObjectReference{
										Name: fmt.Sprintf("%s-config", simulation.Name),
									},
								},
							},
						},
					},
				},
			},
			VolumeClaimTemplates: []corev1.PersistentVolumeClaim{
				{
					ObjectMeta: metav1.ObjectMeta{
						Name: "data",
					},
					Spec: corev1.PersistentVolumeClaimSpec{
						AccessModes: []corev1.PersistentVolumeAccessMode{
							corev1.ReadWriteOnce,
						},
						StorageClassName: &simulation.Spec.Cluster.StorageClass,
						Resources: corev1.ResourceRequirements{
							Requests: corev1.ResourceList{
								corev1.ResourceStorage: resource.MustParse(simulation.Spec.Cluster.StorageSize),
							},
						},
					},
				},
			},
		},
	}

	// Special handling for the first pod to be the orchestrator
	// This could be done with a StatefulSet initialization container
	// For simplicity, we're just setting the role label and expecting the container
	// to behave differently based on the pod name
	// In a real implementation, we might have a different container image for the orchestrator

	// Check if StatefulSet already exists
	existingStatefulSet := &appsv1.StatefulSet{}
	err := r.Get(ctx, types.NamespacedName{
		Name:      statefulSet.Name,
		Namespace: statefulSet.Namespace,
	}, existingStatefulSet)

	if err != nil {
		if errors.IsNotFound(err) {
			// Create new StatefulSet
			if err = r.Create(ctx, statefulSet); err != nil {
				return fmt.Errorf("failed to create StatefulSet: %v", err)
			}
			log.Printf("Created StatefulSet %s", statefulSet.Name)
		} else {
			return fmt.Errorf("failed to check existing StatefulSet: %v", err)
		}
	} else {
		// Update existing StatefulSet if needed
		// Note: Updating StatefulSets can be complex due to update strategies
		// For simplicity, we're just updating certain fields
		existingStatefulSet.Spec.Replicas = statefulSet.Spec.Replicas
		existingStatefulSet.Spec.Template.Spec.Containers[0].Image = statefulSet.Spec.Template.Spec.Containers[0].Image
		existingStatefulSet.Spec.Template.Spec.Containers[0].Resources = statefulSet.Spec.Template.Spec.Containers[0].Resources

		if err = r.Update(ctx, existingStatefulSet); err != nil {
			return fmt.Errorf("failed to update StatefulSet: %v", err)
		}
		log.Printf("Updated StatefulSet %s", statefulSet.Name)
	}

	return nil
}

// FinancialSimulationReconciler implements the reconciler pattern for our CRD
type FinancialSimulationReconciler struct {
	client.Client
	Log           logr.Logger
	Scheme        *runtime.Scheme
	EventChan     chan<- SimulationEvent
	MessageBroker *MessageBroker
	ConfigService *ConfigService
}

// +kubebuilder:rbac:groups=sim.financial.io,resources=financialsimulations,verbs=get;list;watch;create;update;patch;delete
// +kubebuilder:rbac:groups=sim.financial.io,resources=financialsimulations/status,verbs=get;update;patch
// +kubebuilder:rbac:groups=apps,resources=statefulsets,verbs=get;list;watch;create;update;patch;delete
// +kubebuilder:rbac:groups=core,resources=services;configmaps;persistentvolumeclaims,verbs=get;list;watch;create;update;patch;delete
// +kubebuilder:rbac:groups=core,resources=pods,verbs=get;list;watch

// SimulationEvent represents a simulation lifecycle event
type SimulationEvent struct {
	Type       string
	Simulation *FinancialSimulation
	Message    string
	Timestamp  time.Time
}

// Controller contains the core controller logic for kubernetes
type Controller struct {
	clientset     kubernetes.Interface
	queue         workqueue.RateLimitingInterface
	informer      cache.SharedIndexInformer
	eventChan     chan SimulationEvent
	messageBroker *MessageBroker
	configService *ConfigService
}

// NewController creates a new controller
func NewController(clientset kubernetes.Interface, informer cache.SharedIndexInformer, messageBroker *MessageBroker, configService *ConfigService) *Controller {
	queue := workqueue.NewRateLimitingQueue(workqueue.DefaultControllerRateLimiter())

	controller := &Controller{
		clientset:     clientset,
		informer:      informer,
		queue:         queue,
		eventChan:     make(chan SimulationEvent, 100),
		messageBroker: messageBroker,
		configService: configService,
	}

	// Set up event handlers
	informer.AddEventHandler(cache.ResourceEventHandlerFuncs{
		AddFunc: func(obj interface{}) {
			key, err := cache.MetaNamespaceKeyFunc(obj)
			if err == nil {
				queue.Add(key)
				controller.eventChan <- SimulationEvent{
					Type:       "Added",
					Simulation: obj.(*FinancialSimulation),
					Message:    "Simulation created",
					Timestamp:  time.Now(),
				}
			}
		},
		UpdateFunc: func(old interface{}, new interface{}) {
			key, err := cache.MetaNamespaceKeyFunc(new)
			if err == nil {
				queue.Add(key)
				controller.eventChan <- SimulationEvent{
					Type:       "Updated",
					Simulation: new.(*FinancialSimulation),
					Message:    "Simulation updated",
					Timestamp:  time.Now(),
				}
			}
		},
		DeleteFunc: func(obj interface{}) {
			key, err := cache.DeletionHandlingMetaNamespaceKeyFunc(obj)
			if err == nil {
				queue.Add(key)
				controller.eventChan <- SimulationEvent{
					Type:       "Deleted",
					Simulation: obj.(*FinancialSimulation),
					Message:    "Simulation deleted",
					Timestamp:  time.Now(),
				}
			}
		},
	})

	return controller
}

// Run starts the controller
func (c *Controller) Run(stopCh <-chan struct{}) {
	defer c.queue.ShutDown()

	go c.informer.Run(stopCh)

	if !cache.WaitForCacheSync(stopCh, c.informer.HasSynced) {
		log.Println("Timed out waiting for caches to sync")
		return
	}

	go c.eventLoop()

	// Start worker goroutines
	for i := 0; i < 5; i++ {
		go c.worker()
	}

	<-stopCh
}

// eventLoop processes simulation events
func (c *Controller) eventLoop() {
	for event := range c.eventChan {
		// Handle simulation lifecycle events
		log.Printf("Simulation event: %s - %s", event.Type, event.Message)

		// For example, publish the event using the message broker
		statusMsg := SystemStatusMessage{
			NodeID:    "controller",
			Timestamp: event.Timestamp,
			Status:    event.Type,
			Message:   event.Message,
			SimTime:   event.Timestamp.UnixNano(),
		}

		if err := c.messageBroker.PublishSystemStatus(statusMsg); err != nil {
			log.Printf("Failed to publish system status: %v", err)
		}
	}
}

// worker processes items from the work queue
func (c *Controller) worker() {
	for c.processNextItem() {
	}
}

// processNextItem processes the next item from the work queue
func (c *Controller) processNextItem() bool {
	// Get next item from queue
	key, quit := c.queue.Get()
	if quit {
		return false
	}
	defer c.queue.Done(key)

	// Process the item
	err := c.processItem(key.(string))
	if err == nil {
		// No error, reset the rate limiter for this item
		c.queue.Forget(key)
	} else if c.queue.NumRequeues(key) < 5 {
		// There was an error, requeue the item
		log.Printf("Error processing %s (will retry): %v", key, err)
		c.queue.AddRateLimited(key)
	} else {
		// Too many retries
		log.Printf("Error processing %s (giving up): %v", key, err)
		c.queue.Forget(key)
	}

	return true
}

// processItem processes a single work item
func (c *Controller) processItem(key string) error {
	log.Printf("Processing key: %s", key)

	// Split the key into namespace and name
	namespace, name, err := cache.SplitMetaNamespaceKey(key)
	if err != nil {
		return fmt.Errorf("invalid resource key: %s", key)
	}

	// Get the FinancialSimulation resource
	obj, exists, err := c.informer.GetIndexer().GetByKey(key)
	if err != nil {
		return fmt.Errorf("error fetching object with key %s: %v", key, err)
	}

	if !exists {
		// It was deleted
		log.Printf("FinancialSimulation %s/%s was deleted", namespace, name)
		return nil
	}

	// Cast to FinancialSimulation
	simulation, ok := obj.(*FinancialSimulation)
	if !ok {
		return fmt.Errorf("error casting object to FinancialSimulation")
	}

	// Handle the simulation - implementation would call appropriate methods of SimulationController
	log.Printf("Handling FinancialSimulation %s/%s", namespace, name)

	// In a real implementation, we would use a proper controller-runtime reconciler

	return nil
}

// StartKubernetesController sets up and starts the Kubernetes controller
func StartKubernetesController(kubeconfig string, messageBroker *MessageBroker, configService *ConfigService) error {
	// Create kubernetes client config
	config, err := clientcmd.BuildConfigFromFlags("", kubeconfig)
	if err != nil {
		return fmt.Errorf("error building kubeconfig: %v", err)
	}

	// Create clientset
	clientset, err := kubernetes.NewForConfig(config)
	if err != nil {
		return fmt.Errorf("error creating clientset: %v", err)
	}

	// Create manager with controller-runtime
	mgr, err := ctrl.NewManager(config, ctrl.Options{
		Scheme: runtime.NewScheme(),
	})
	if err != nil {
		return fmt.Errorf("error creating manager: %v", err)
	}

	// Add FinancialSimulation type to scheme
	if err := AddToScheme(mgr.GetScheme()); err != nil {
		return fmt.Errorf("error adding FinancialSimulation to scheme: %v", err)
	}

	// Create controller
	controller, err := NewSimulationController(mgr, messageBroker, configService)
	if err != nil {
		return fmt.Errorf("error creating controller: %v", err)
	}

	// Set up controller with manager
	if err := controller.SetupWithManager(mgr); err != nil {
		return fmt.Errorf("error setting up controller: %v", err)
	}

	// Start manager
	log.Println("Starting manager")
	if err := mgr.Start(ctrl.SetupSignalHandler()); err != nil {
		return fmt.Errorf("error running manager: %v", err)
	}

	return nil
}
