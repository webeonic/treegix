

package scheduler

import (
	"container/heap"
)

type pluginHeap []*pluginAgent

func (h pluginHeap) Len() int {
	return len(h)
}

func (h pluginHeap) Less(i, j int) bool {
	if left := h[i].peekTask(); left != nil {
		if right := h[j].peekTask(); right != nil {
			return left.getScheduled().Before(right.getScheduled())
		} else {
			return false
		}
	} else {
		return true
	}
}

func (h pluginHeap) Swap(i, j int) {
	h[i], h[j] = h[j], h[i]
	h[i].index = i
	h[j].index = j
}

// Push -
func (h *pluginHeap) Push(x interface{}) {
	// Push and Pop use pointer receivers because they modify the slice's length,
	// not just its contents.
	p := x.(*pluginAgent)
	p.index = len(*h)
	*h = append(*h, p)
}

// Pop -
func (h *pluginHeap) Pop() interface{} {
	old := *h
	n := len(old)
	p := old[n-1]
	*h = old[0 : n-1]
	p.index = -1
	return p
}

// Peek -
func (h *pluginHeap) Peek() *pluginAgent {
	if len(*h) == 0 {
		return nil
	}
	return (*h)[0]
}

func (h *pluginHeap) Update(p *pluginAgent) {
	if p.index != -1 {
		heap.Fix(h, p.index)
	}
}

func (h *pluginHeap) Remove(p *pluginAgent) {
	if p.index != -1 {
		heap.Remove(h, p.index)
	}
}
