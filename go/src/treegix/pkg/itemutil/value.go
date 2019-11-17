

package itemutil

import (
	"fmt"
	"strconv"
	"time"
	"treegix/pkg/plugin"
)

const StateNotSupported = 1

func ValueToResult(itemid uint64, ts time.Time, v interface{}) (result *plugin.Result) {
	var value string
	switch v.(type) {
	case *plugin.Result:
		return v.(*plugin.Result)
	case plugin.Result:
		r := v.(plugin.Result)
		return &r
	case string:
		value = v.(string)
	case *string:
		value = *v.(*string)
	case int:
		value = strconv.FormatInt(int64(v.(int)), 10)
	case int64:
		value = strconv.FormatInt(v.(int64), 10)
	case uint:
		value = strconv.FormatUint(uint64(v.(uint)), 10)
	case uint64:
		value = strconv.FormatUint(v.(uint64), 10)
	case float32:
		value = strconv.FormatFloat(float64(v.(float32)), 'f', 6, 64)
	case float64:
		value = strconv.FormatFloat(v.(float64), 'f', 6, 64)
	default:
		// note that this conversion is slow and it's better to return known value type
		value = fmt.Sprintf("%v", v)
	}
	return &plugin.Result{Itemid: itemid, Value: &value, Ts: ts}
}
