

package scheduler

import (
	"container/heap"
	"time"
)

// performer interface is implemented by task to
type performer interface {
	// returns the task plugin
	getPlugin() *pluginAgent
	// performs the task, this function is called in a separate goroutine
	perform(s Scheduler)
	// reschedules the task, returns false if the task has been expired
	reschedule(now time.Time) error
	// returns time the task has been scheduled to perform
	getScheduled() time.Time
	// returns task weight
	getWeight() int
	// returns task index in plugin task queue
	getIndex() int
	// sets task index in the plugin task queue
	setIndex(index int)
	// returns true if the task is active
	isActive() bool
	// deactivates task, removing from plugin task queue if necessary
	deactivate()
	// true if the task has to be rescheduled after performing
	isRecurring() bool
}

// performerHeap -
type performerHeap []performer

func (h performerHeap) Len() int {
	return len(h)
}

func (h performerHeap) Less(i, j int) bool {
	return h[i].getScheduled().Before(h[j].getScheduled())
}

func (h performerHeap) Swap(i, j int) {
	h[i], h[j] = h[j], h[i]
	h[i].setIndex(i)
	h[j].setIndex(j)
}

// Push -
func (h *performerHeap) Push(x interface{}) {
	// Push and Pop use pointer receivers because they modify the slice's length,
	// not just its contents.
	p := x.(performer)
	p.setIndex(len(*h))
	*h = append(*h, p)
}

// Pop -
func (h *performerHeap) Pop() interface{} {
	old := *h
	n := len(old)
	p := old[n-1]
	// clear slice slot, so the performer can be garbage collected later
	old[n-1] = nil
	*h = old[0 : n-1]
	p.setIndex(-1)
	return p
}

// Peek -
func (h *performerHeap) Peek() performer {
	if len(*h) == 0 {
		return nil
	}
	return (*h)[0]
}

func (h *performerHeap) Update(p performer) {
	if p.getIndex() != -1 {
		heap.Fix(h, p.getIndex())
	}
}
