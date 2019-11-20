

package main

import (
	"encoding/json"
	"flag"
	"fmt"
	"io/ioutil"
	"os"
	"strconv"
	"time"
	"treegix/pkg/conf"
	"treegix/pkg/log"
	"treegix/pkg/trxcomms"
)

type MockServerOptions struct {
	LogType          string `conf:",,,console"`
	LogFile          string `conf:",optional"`
	DebugLevel       int    `conf:",,0:5,3"`
	Port             int    `conf:",,1:65535,10051"`
	Timeout          int    `conf:",,1:30,5"`
	ActiveChecksFile string `conf:",optional"`
}

var options MockServerOptions

func handleConnection(c *trxcomms.Connection, tFlag int) {
	defer c.Close()

	js, err := c.Read(time.Second * time.Duration(tFlag))
	if err != nil {
		log.Warningf("Read failed: %s\n", err)
		return
	}

	log.Debugf("got '%s'", string(js))

	var pairs map[string]interface{}
	if err := json.Unmarshal(js, &pairs); err != nil {
		log.Warningf("Unmarshal failed: %s\n", err)
		return
	}

	switch pairs["request"] {
	case "active checks":
		activeChecks, err := ioutil.ReadFile(options.ActiveChecksFile)
		if err == nil {
			err = c.Write(activeChecks, time.Second*time.Duration(tFlag))
		}
		if err != nil {
			log.Warningf("Write failed: %s\n", err)
			return
		}
	case "agent data":
		err = c.WriteString("{\"response\":\"success\",\"info\":\"processed: 0; failed: 0; total: 0; seconds spent: 0.042523\"}", time.Second*time.Duration(tFlag))
		if err != nil {
			log.Warningf("Write failed: %s\n", err)
			return
		}

	default:
		log.Warningf("Unsupported request: %s\n", pairs["request"])
		return
	}

}

func main() {
	var confFlag string
	const (
		confDefault     = "mock_server.conf"
		confDescription = "Path to the configuration file"
	)
	flag.StringVar(&confFlag, "config", confDefault, confDescription)
	flag.StringVar(&confFlag, "c", confDefault, confDescription+" (shorhand)")

	var foregroundFlag bool
	const (
		foregroundDefault     = true
		foregroundDescription = "Run Treegix mock server in foreground"
	)
	flag.BoolVar(&foregroundFlag, "foreground", foregroundDefault, foregroundDescription)
	flag.BoolVar(&foregroundFlag, "f", foregroundDefault, foregroundDescription+" (shorhand)")
	flag.Parse()

	if err := conf.Load(confFlag, &options); err != nil {
		fmt.Fprintf(os.Stderr, "%s\n", err.Error())
		os.Exit(1)
	}

	var logType, logLevel int
	switch options.LogType {
	case "console":
		logType = log.Console
	case "file":
		logType = log.File
	}
	switch options.DebugLevel {
	case 0:
		logLevel = log.Info
	case 1:
		logLevel = log.Crit
	case 2:
		logLevel = log.Err
	case 3:
		logLevel = log.Warning
	case 4:
		logLevel = log.Debug
	case 5:
		logLevel = log.Trace
	}

	if err := log.Open(logType, logLevel, options.LogFile, 0); err != nil {
		fmt.Fprintf(os.Stderr, "Cannot initialize logger: %s\n", err.Error())
		os.Exit(1)
	}

	greeting := fmt.Sprintf("Starting Treegix Mock server [(hostname placeholder)]. (version placeholder)")
	log.Infof(greeting)

	if foregroundFlag {
		if options.LogType != "console" {
			fmt.Println(greeting)
		}
		fmt.Println("Press Ctrl+C to exit.")
	}

	log.Infof("using configuration file: %s", confFlag)

	listener, err := trxcomms.Listen(":" + strconv.Itoa(options.Port))
	if err != nil {
		log.Critf("Listen failed: %s\n", err)
		return
	}
	defer listener.Close()

	for {
		c, err := listener.Accept()
		if err != nil {
			log.Critf("Accept failed: %s\n", err)
			return
		}
		go handleConnection(c, options.Timeout)
	}
}
