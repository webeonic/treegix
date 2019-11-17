

package statuslistener

import (
	"context"
	"fmt"
	"net"
	"net/http"
	"time"
	"treegix/internal/agent"
	"treegix/internal/agent/scheduler"
	"treegix/pkg/log"
	"treegix/pkg/version"
)

var srv http.Server

func getConf(confFilePath string) (s string) {
	s = fmt.Sprintf("Treegix Agent 2 [%s]. (%s)\n"+
		"using configuration file: %s\nServerActive: %s\nListenPort: %d\n\n",
		agent.Options.Hostname, version.Long(),
		confFilePath, agent.Options.ServerActive, agent.Options.ListenPort)

	return
}

func Start(taskManager scheduler.Scheduler, confFilePath string) (err error) {
	var l net.Listener

	if l, err = net.Listen("tcp", fmt.Sprintf(":%d", agent.Options.StatusPort)); err != nil {
		return err
	}

	log.Debugf("starting status listener")

	mux := http.NewServeMux()
	mux.Handle("/status", http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		log.Debugf("received status request from %s", r.RemoteAddr)
		fmt.Fprintf(w, getConf(confFilePath))
		fmt.Fprintf(w, taskManager.Query("metrics"))
	}))

	srv = http.Server{Addr: fmt.Sprintf(":%d", agent.Options.StatusPort), Handler: mux}
	go func() {
		defer log.PanicHook()
		err = srv.Serve(l)
		log.Debugf("%s", err.Error())
	}()

	return nil
}

func Stop() {
	// shut down gracefully, but wait no longer than time defined in configuration parameter Timeout
	ctx, cancel := context.WithTimeout(context.Background(), time.Second*time.Duration(agent.Options.Timeout))
	defer cancel()

	if err := srv.Shutdown(ctx); err != nil {
		log.Errf("cannot gacefully stop status listener: %s", err.Error())
	} else {
		log.Debugf("status listener has been stopped")
	}
}
