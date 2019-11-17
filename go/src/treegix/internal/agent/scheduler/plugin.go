

package scheduler

import (
	"container/heap"
	"treegix/pkg/plugin"
)

type pluginAgent struct {
	impl         plugin.Accessor
	tasks        performerHeap
	capacity     int
	usedCapacity int
	index        int
	// refcount us used to track plugin usage by request batches
	refcount int
}

func (p *pluginAgent) peekTask() performer {
	if len(p.tasks) == 0 {
		return nil
	}
	return p.tasks[0]
}

func (p *pluginAgent) popTask() performer {
	if len(p.tasks) == 0 {
		return nil
	}
	task := p.tasks[0]
	heap.Pop(&p.tasks)
	return task
}

func (p *pluginAgent) enqueueTask(task performer) {
	heap.Push(&p.tasks, task)
}

func (p *pluginAgent) removeTask(index int) {
	heap.Remove(&p.tasks, index)
}

func (p *pluginAgent) reserveCapacity(task performer) {
	p.usedCapacity += task.getWeight()
}

func (p *pluginAgent) releaseCapacity(task performer) {
	p.usedCapacity -= task.getWeight()
}

func (p *pluginAgent) queued() bool {
	return p.index != -1
}

func (p *pluginAgent) hasCapacity() bool {
	return len(p.tasks) != 0 && p.capacity-p.usedCapacity >= p.tasks[0].getWeight()
}

func (p *pluginAgent) active() bool {
	return p.refcount != 0
}

func (p *pluginAgent) name() string {
	return p.impl.Name()
}
