# Multicast DNS (.local names)

air/OS resolves and answers `.local` names with multicast DNS (RFC 6762),
which WebRTC needs: a browser hides a peer connection's local addresses behind
random names such as `5f3a9c1e-....local`, which the other side resolves on
the local network. Two parts, since 2026-10-27 (summit#119):

**Resolving.** `getaddrinfo()` (and everything built on it) asks the network
for a name ending in `.local` instead of the DNS server: a one-shot query to
224.0.0.251 port 5353 (RFC 6762 section 5.1), asked again after a second, for
three seconds at most. Answers from other machines' responders (Avahi, Apple's
mDNSResponder, `mdns_server` on another air/OS machine) and from this
machine's `mdns_server` are taken. IPv4 queries only; AAAA records in the
answers are used.

**Answering.** `mdns_server` (`application/x-vnd.vos-mdns_server`, started
on demand by the launch daemon) answers A questions for names that programs
register. It publishes nothing by itself, not even the machine's name.

| Message | Fields | Reply |
| --- | --- | --- |
| `MDNS_REGISTER_NAME` (`'mdrg'`) | `name` (string, ending in `.local`), `address` (string, IPv4, optional) | `status` (int32) |
| `MDNS_UNREGISTER_NAME` (`'mdur'`) | `name` (string) | `status` (int32) |

Without an address, a question is answered with the address of the interface
on the asker's network. A name is answered until it is unregistered or until
the team that registered it is gone. A registered name is announced twice and
withdrawn with a goodbye (TTL 0). Registering a name another running program
holds fails with `B_NAME_IN_USE`. Constants: `<mdns_server.h>` (private
headers, `headers/private/net`).

```cpp
BMessenger server("application/x-vnd.vos-mdns_server");
BMessage request('mdrg');
request.AddString("name", "5f3a9c1e-2b7d-4c8e-9a1f-3d6b8e2c4a70.local");
BMessage reply;
if (server.SendMessage(&request, &reply) == B_OK
	&& reply.GetInt32("status", B_ERROR) == B_OK) {
	// peers can now resolve it
}
```

Before this, multicast datagrams sent where only the default route matched
went to the gateway's Ethernet address (fixed in the network stack's datalink
module at the same time); without that fix no query reached other machines
reliably.

Tests: `mdns_test <name>.local [seconds]` (src/tests/servers/mdns) registers,
resolves, unregisters and checks that the name is gone.
