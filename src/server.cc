/*
 * Traffic generator
 * 
 * Addy Bombeke <addy.bombeke@ugent.be>
 * Douwe De Bock <douwe.debock@ugent.be>
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <iostream>
#include <thread>
#include <time.h>
#include <signal.h>
#include <time.h>
#include <string.h>

#define RINA_PREFIX "traffic-generator"
#include <librina/logs.h>

#include "server.h"

using namespace std;
using namespace rina;

void Server::run()
{
        applicationRegister();

        for(;;) {
                IPCEvent * event = ipcEventProducer->eventWait();
                Flow * flow = nullptr;
                unsigned int port_id;

                if (!event)
                        return;

                switch (event->eventType) {

                case REGISTER_APPLICATION_RESPONSE_EVENT:
                        ipcManager->commitPendingRegistration(event->sequenceNumber,
                                                             dynamic_cast<RegisterApplicationResponseEvent*>(event)->DIFName);
                        break;

                case UNREGISTER_APPLICATION_RESPONSE_EVENT:
                        ipcManager->appUnregistrationResult(event->sequenceNumber,
                                                            dynamic_cast<UnregisterApplicationResponseEvent*>(event)->result == 0);
                        break;

                case FLOW_ALLOCATION_REQUESTED_EVENT:
			{
				flow = ipcManager->allocateFlowResponse(*dynamic_cast<FlowRequestEvent*>(event), 0, true);
				LOG_INFO("New flow allocated [port-id = %d]", flow->getPortId());
				thread t(&Server::startReceive, this, flow);
				t.detach();

				break;
			}
                case FLOW_DEALLOCATED_EVENT:
                        port_id = dynamic_cast<FlowDeallocatedEvent*>(event)->portId;
                        ipcManager->flowDeallocated(port_id);
                        LOG_INFO("Flow torn down remotely [port-id = %d]", port_id);
                        break;

                default:
                        LOG_INFO("Server got new event of type %d",
                                        event->eventType);
                        break;
                }
        }
}

static unsigned long msElapsed(const struct timespec &start,
		const struct timespec &end);

void Server::startReceive(Flow * flow)
{
	unsigned long long count;
	unsigned int duration;
	unsigned int sduSize;

	char initData[sizeof(count) + sizeof(duration) + sizeof(sduSize)];

	flow->readSDU(initData, sizeof(count) + sizeof(duration) + sizeof(sduSize));

	memcpy(&count, initData, sizeof(count));
	memcpy(&duration, &initData[sizeof(count)], sizeof(duration));
	memcpy(&sduSize, &initData[sizeof(count) + sizeof(duration)], sizeof(sduSize));

	char response[] = "Go ahead!";
	struct timespec start;
	struct timespec end;

	clock_gettime(CLOCK_REALTIME, &start);
	flow->writeSDU(response, sizeof(response));

	LOG_INFO("Starting test:\n\tduration: %u\n\tcount: %llu\n\tsduSize: %u", duration, count, sduSize);

	int running = true;
	char data[sduSize];

	flow->readSDU(data, sduSize);
	//clock_gettime(CLOCK_REALTIME, &end);

	/*
	double timeout = 4000 * (((end.tv_sec - start.tv_sec) * 1000000
				- (end.tv_nsec - start.tv_nsec) / 1000));
	struct itimerspec itimer;
	itimer.it_interval.tv_sec = 0;
	itimer.it_interval.tv_nsec = 0;
	itimer.it_value.tv_sec = timeout / 1000000000;
	itimer.it_value.tv_nsec = timeout % 1000000000;
	*/
	bool timeTest;
	if (duration) {
		timeTest = true;
		struct sigevent event;
		struct itimerspec durationTimer;
		timer_t timerId;

		event.sigev_notify = SIGEV_THREAD;
		event.sigev_value.sival_ptr = &running;
		event.sigev_notify_function = &timesUp;

		timer_create(CLOCK_REALTIME, &event, &timerId);

		durationTimer.it_interval.tv_sec = 0;
		durationTimer.it_interval.tv_nsec = 0;
		durationTimer.it_value.tv_sec = duration;
		durationTimer.it_value.tv_nsec = 0;

		timer_settime(timerId, 0, &durationTimer, NULL);
	} else
		timeTest = false;

	unsigned long long totalSdus = 1;
	unsigned long long totalBytes = 0;
	clock_gettime(CLOCK_REALTIME, &start);
	while (running) {
		totalBytes += flow->readSDU(data, sduSize);
		totalSdus++;
		
		if (!timeTest) {
			if (totalSdus >= count)
				running = 0;
		}
	}
	clock_gettime(CLOCK_REALTIME, &end);
	unsigned long ms = msElapsed(start, end);

	LOG_INFO("Received %llu SDUs and %llu bytes in %lu ms", totalSdus, totalBytes, ms);
	LOG_INFO("\t%.4f Mbps", static_cast<float>((totalBytes * 8.0) / (ms * 1000)));
}

static unsigned long msElapsed(const struct timespec &start,
		const struct timespec &end)
{
	return ((end.tv_sec - start.tv_sec) * 1000000000
			+ (end.tv_nsec - start.tv_nsec)) / 1000000;
}

void Server::timesUp(sigval_t val)
{
	int *running = (int *)val.sival_ptr;

	*running = 0;
}