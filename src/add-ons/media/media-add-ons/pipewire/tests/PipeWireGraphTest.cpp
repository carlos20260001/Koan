/*
 * Copyright 2026, Koan contributors. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

/*!	The PipeWire side of the media add-on, without the Media Kit: connects,
	lists the sinks and sources the add-on would publish as flavors, and,
	when there is a sink, plays half a second of a tone through a stream to
	PipeWire's default sink and checks that PipeWire took it from the ring.
	Exit 77 (skipped) without a running PipeWire.
*/


#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "PipeWireGraph.h"


static double
now()
{
	struct timespec time;
	clock_gettime(CLOCK_MONOTONIC, &time);
	return time.tv_sec + time.tv_nsec / 1e9;
}


int
main()
{
	PipeWireGraph graph;
	int status = graph.Init("pipewire_graph_test");
	if (status == 0)
		status = graph.Sync(3000);
	if (status != 0) {
		printf("no PipeWire (%s): skipped\n", strerror(-status));
		return 77;
	}

	std::vector<PipeWireEndpoint> endpoints;
	graph.GetEndpoints(endpoints);
	int sinks = 0;
	for (size_t i = 0; i < endpoints.size(); i++) {
		const PipeWireEndpoint& endpoint = endpoints[i];
		printf("%s %u: %s (%s)\n", endpoint.sink ? "sink  " : "source",
			endpoint.id, endpoint.description.c_str(), endpoint.name.c_str());
		if (endpoint.sink)
			sinks++;
	}
	printf("%zu sinks and sources\n", endpoints.size());

	if (sinks == 0) {
		printf("no sink: playback not tried\n");
		return 0;
	}

	const uint32_t rate = 48000;
	const uint32_t channels = 2;
	PipeWireStream stream(&graph, true, NULL, rate, channels,
		"pipewire-graph-test");
	status = stream.Connect();
	if (status != 0) {
		printf("FAIL: cannot connect a playback stream: %s\n",
			strerror(-status));
		return 1;
	}

	// half a second of 440 Hz, quietly
	const size_t total = rate / 2;
	float frames[256 * 2];
	size_t written = 0;
	double deadline = now() + 5;
	while (written < total && now() < deadline) {
		size_t count = 256;
		if (count > total - written)
			count = total - written;
		for (size_t i = 0; i < count; i++) {
			float sample = 0.05f * sinf(2 * M_PI * 440 * (written + i) / rate);
			frames[2 * i] = frames[2 * i + 1] = sample;
		}
		size_t done = stream.Write(frames, count);
		written += done;
		if (done < count)
			usleep(5000);
	}

	while (stream.Queued() > 0 && now() < deadline)
		usleep(10000);

	size_t left = stream.Queued();
	stream.Disconnect();
	graph.Uninit();

	if (written < total || left > 0) {
		printf("FAIL: PipeWire took %zu of %zu frames\n", written - left,
			total);
		return 1;
	}
	printf("played %zu frames through the default sink\n", total);
	return 0;
}
