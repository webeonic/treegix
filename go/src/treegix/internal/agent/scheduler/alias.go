

package scheduler

import (
	"fmt"
	"strings"
	"treegix/internal/agent"
	"treegix/pkg/itemutil"
)

type keyAlias struct {
	name, key string
}

func (m *Manager) loadAlias(options agent.AgentOptions) (err error) {
	m.aliases = make([]keyAlias, 0)
	for _, data := range options.Alias {
		var name, key string
		if name, key, err = itemutil.ParseAlias(data); err != nil {
			return fmt.Errorf("cannot add alias \"%s\": %s", data, err)
		}
		for _, existingAlias := range m.aliases {
			if existingAlias.name == name {
				return fmt.Errorf("failed to add Alias \"%s\": duplicate name", name)
			}
		}
		m.aliases = append(m.aliases, keyAlias{name: name, key: key})
	}
	return nil
}

func (m *Manager) getAlias(orig string) string {
	if _, _, err := itemutil.ParseKey(orig); err != nil {
		return orig
	}
	for _, a := range m.aliases {
		if strings.Compare(a.name, orig) == 0 {
			return a.key
		}
	}
	for _, a := range m.aliases {
		aliasLn := len(a.name)
		if aliasLn <= 3 || a.name[aliasLn-3:] != `[*]` {
			continue
		}
		if aliasLn-2 > len(orig) {
			return orig
		}
		if strings.Compare(a.name[:aliasLn-2], orig[:aliasLn-2]) != 0 {
			continue
		}
		if len(a.key) <= 3 || a.key[len(a.key)-3:] != `[*]` {
			return a.key
		}
		return string(a.key[:len(a.key)-3] + orig[len(a.name)-3:])
	}
	return orig
}
