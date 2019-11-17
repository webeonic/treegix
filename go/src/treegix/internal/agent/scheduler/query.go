

package scheduler

import (
	"errors"
	"fmt"
	"sort"
	"strings"
	"treegix/pkg/plugin"
)

type pluginMetrics struct {
	ref     *pluginAgent
	metrics []*plugin.Metric
}

func (m *Manager) getStatus() (result string) {
	var status strings.Builder
	agents := make(map[plugin.Accessor]*pluginMetrics)
	infos := make([]*pluginMetrics, 0, len(m.plugins))
	for _, p := range m.plugins {
		if _, ok := agents[p.impl]; !ok {
			info := &pluginMetrics{ref: p, metrics: make([]*plugin.Metric, 0)}
			infos = append(infos, info)
			agents[p.impl] = info
		}
	}

	for _, metric := range plugin.Metrics {
		if info, ok := agents[metric.Plugin]; ok {
			info.metrics = append(info.metrics, metric)
		}
	}
	sort.Slice(infos, func(i, j int) bool {
		return infos[i].ref.name() < infos[j].ref.name()
	})

	for _, info := range infos {
		status.WriteString(fmt.Sprintf("[%s]\nactive: %t\ncapacity: %d/%d\ntasks: %d\n",
			info.ref.name(), info.ref.active(), info.ref.usedCapacity, info.ref.capacity, len(info.ref.tasks)))
		sort.Slice(info.metrics, func(l, r int) bool { return info.metrics[l].Key < info.metrics[r].Key })
		for _, metric := range info.metrics {
			status.WriteString(metric.Key)
			status.WriteString(": ")
			status.WriteString(metric.Description)
			status.WriteString("\n")
		}
		status.WriteString("\n")
	}
	return status.String()
}

func (m *Manager) processQuery(r *queryRequest) (text string, err error) {
	switch r.command {
	case "metrics":
		return m.getStatus(), nil
	default:
		return "", errors.New("unknown request")
	}
}
