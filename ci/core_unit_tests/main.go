package main

import (
	"context"
	"errors"
	"fmt"
	"os"
	"strings"
	"time"

	"dagger.io/dagger"
)

const (
	// Max number of files compiled by Ninja if there are compilation errors
	NB_MAX_BUILD_ERRORS string = "999"
)

func main() {
	err := buildAnRunCurrentDirectory()
	if err != nil {
		fmt.Println(err)
		fmt.Println("exit_failure")
		os.Exit(1)
	}
	fmt.Println("exit_success")
	os.Exit(0)
}

func buildAnRunCurrentDirectory() error {
	// Get background context
	ctx := context.Background()

	// Initialize dagger client
	client, err := dagger.Connect(ctx,
		dagger.WithLogOutput(os.Stdout), // output the logs to the standard output
		dagger.WithWorkdir("../.."),     // go to cubzh root directory
	)
	if err != nil {
		return err
	}
	defer client.Close()

	// create container with source files
	ciContainer := client.Container().From("voxowl/cpp-build-env:14.0.0")

	// retrieve container architecture and provide it as ENVAR inside the container
	{
		platform, err := ciContainer.Platform(ctx)
		if err != nil {
			return err
		}
		// platform is of the form "linux/arm64"
		// architecture is the second par of the platform string, after the '/'
		architecture := strings.Split(string(platform), "/")[1]
		ciContainer = ciContainer.WithEnvVariable("CUBZH_ARCH", architecture)
	}

	// create a reference to host root dir
	dirOpts := dagger.HostDirectoryOpts{
		// include only the following directories
		Include: []string{"core", "deps/libz"},
	}
	src := client.Host().Directory(".", dirOpts)

	// mount host directory to container and go into it
	ciContainer = ciContainer.WithMountedDirectory("/project", src)
	ciContainer = ciContainer.WithWorkdir("/project/core/tests/cmake")

	// execute build commands
	ciContainer = ciContainer.WithExec([]string{"cmake", "-G", "Ninja", "."})

	// To know whether the execution succeeded, we need to force the evaluation
	// of the pipeline after an using Sync, and then
	ciContainer, err = ciContainer.Sync(ctx)
	if err != nil {
		var execErr *dagger.ExecError
		if errors.As(err, &execErr) {
			// err is an ExecError
			fmt.Println(execErr.Stderr)
			return errors.New("cmake error")
		}
		fmt.Println("error syncing the pipeline after exec")
		return err
	}

	fmt.Println("Running tests in container...")

	// Flags after `--` are transmitted as-is to the build system (Ninja, here)
	// Ninja will stop if this number of errors is reached : NB_MAX_BUILD_ERRORS
	ciContainer = ciContainer.WithExec([]string{"cmake", "--build", ".", "--clean-first", "--", "-k", NB_MAX_BUILD_ERRORS})

	// To know whether the execution succeeded, we need to force the evaluation
	// of the pipeline after an using Sync, and then
	ciContainer, err = ciContainer.Sync(ctx)
	if err != nil {
		var execErr *dagger.ExecError
		if errors.As(err, &execErr) {
			// err is an ExecError
			fmt.Println(execErr.Stderr)
			return errors.New("cmake --build error")
		}
		fmt.Println("error syncing the pipeline after exec")
		return err
	}

	// exec compiled unit tests program
	ciContainer = ciContainer.WithExec([]string{"./unit_tests"})
	output, err := ciContainer.Stdout(ctx)
	time.Sleep(time.Second * 1) // sleep needed when tests fail (race condition?)
	if err != nil {
		return err
	}
	fmt.Println(output)

	// time.Sleep(time.Second * 1) // sleep needed when tests fail (race condition?)

	// To know whether the execution succeeded, we need to force the evaluation
	// of the pipeline after an using Sync, and then
	ciContainer, err = ciContainer.Sync(ctx)
	if err != nil {
		var execErr *dagger.ExecError
		if errors.As(err, &execErr) {
			// err is an ExecError
			fmt.Println(execErr.Stderr)
			return errors.New("running error")
		}
		fmt.Println("error syncing the pipeline after exec")
		return err
	}

	fmt.Println("Tests done!")
	return nil
}
