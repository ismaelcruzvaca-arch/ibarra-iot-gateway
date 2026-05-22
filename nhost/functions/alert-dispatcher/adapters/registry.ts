// ============================================================================
// registry.ts — Channel adapter registry
//
// Provides a Record<ChannelType, AlertDispatcherAdapter> mapping that the
// DispatcherEngine uses to look up the correct adapter for each channel type.
// Adapters are lazily registered so they can be populated in any order.
// ============================================================================

import type { AlertDispatcherAdapter } from '../types';
import type { ChannelType } from '../types';

/**
 * Supported notification channel types (matches the CHECK constraint in up.sql).
 *
 * All six channel types are defined in types.ts:
 *   'telegram' | 'whatsapp' | 'slack' | 'email' | 'torreta' | 'push'
 *
 * Each type maps to a dedicated adapter file in this directory:
 *   telegram.ts, whatsapp.ts, slack.ts, email.ts, torreta.ts, push.ts
 */

/**
 * Registry holding one adapter per supported channel type.
 * Adapters are registered via registerAdapter() and retrieved via getAdapters().
 */
export class AdapterRegistry {
  private adapters: Partial<Record<ChannelType, AlertDispatcherAdapter>> = {};

  /**
   * Registers an adapter for a given channel type.
   * Throws if an adapter is already registered for that type.
   */
  registerAdapter(channelType: ChannelType, adapter: AlertDispatcherAdapter): void {
    if (this.adapters[channelType]) {
      throw new Error(`Adapter already registered for channel type: ${channelType}`);
    }
    this.adapters[channelType] = adapter;
  }

  /**
   * Returns the adapter for a given channel type, or undefined if not registered.
   */
  getAdapter(channelType: ChannelType): AlertDispatcherAdapter | undefined {
    return this.adapters[channelType];
  }

  /**
   * Returns the full record of registered adapters.
   * Only includes channel types that have been registered.
   */
  getAllAdapters(): Record<ChannelType, AlertDispatcherAdapter> {
    return this.adapters as Record<ChannelType, AlertDispatcherAdapter>;
  }
}

/**
 * Convenience function to create a pre-populated registry from a map.
 */
export function createRegistry(
  adapters: Record<ChannelType, AlertDispatcherAdapter>,
): AdapterRegistry {
  const registry = new AdapterRegistry();
  for (const [channelType, adapter] of Object.entries(adapters)) {
    registry.registerAdapter(channelType as ChannelType, adapter);
  }
  return registry;
}

/**
 * Default empty registry instance.
 * Adapters will be populated as they are implemented (Phase 3).
 */
export const defaultRegistry = createRegistry({} as Record<ChannelType, AlertDispatcherAdapter>);
