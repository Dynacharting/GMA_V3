#pragma once

// Umbrella header for connector authors. Including this brings in IConnector,
// EngineRegistries, and every engine extension-point registry so a connector
// implementation can write a single include instead of seven.
//
// Engine code should still include the specific headers it needs.

#include "krn/engine/IConnector.hpp"
#include "krn/engine/EngineRegistries.hpp"
#include "krn/engine/EventTypeRegistry.hpp"
#include "krn/engine/EventComputerRegistry.hpp"
#include "krn/engine/NodeTypeRegistry.hpp"
#include "krn/engine/IngressRegistry.hpp"
#include "krn/engine/ConfigNamespaceRegistry.hpp"
#include "krn/engine/ProtocolIngressRegistry.hpp"
