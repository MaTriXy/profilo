/*
 * Copyright (C) 2013 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef ART_RUNTIME_JDWP_OBJECT_REGISTRY_H_
#define ART_RUNTIME_JDWP_OBJECT_REGISTRY_H_

#include <museum/8.1.0/libnativehelper/jni.h>
#include <museum/8.1.0/external/libcxx/stdint.h>

#include <museum/8.1.0/external/libcxx/map>

#include <museum/8.1.0/art/runtime/base/casts.h>
#include <museum/8.1.0/art/runtime/handle.h>
#include <museum/8.1.0/art/runtime/jdwp/jdwp.h>
#include <museum/8.1.0/art/runtime/obj_ptr.h>
#include <museum/8.1.0/art/runtime/safe_map.h>

namespace facebook { namespace museum { namespace MUSEUM_VERSION { namespace art {

namespace mirror {
  class Object;
  class Class;
}  // namespace mirror

struct ObjectRegistryEntry {
  // Is jni_reference a weak global or a regular global reference?
  jobjectRefType jni_reference_type;

  // The reference itself.
  jobject jni_reference;

  // A reference count, so we can implement DisposeObject.
  int32_t reference_count;

  // The corresponding id, so we only need one map lookup in Add.
  JDWP::ObjectId id;

  // The identity hash code of the object. This is the same as the key
  // for object_to_entry_. Store this for DisposeObject().
  int32_t identity_hash_code;
};
std::ostream& operator<<(std::ostream& os, const ObjectRegistryEntry& rhs);

// Tracks those objects currently known to the debugger, so we can use consistent ids when
// referring to them. Normally we keep JNI weak global references to objects, so they can
// still be garbage collected. The debugger can ask us to retain objects, though, so we can
// also promote references to regular JNI global references (and demote them back again if
// the debugger tells us that's okay).
class ObjectRegistry {
 public:
  ObjectRegistry();
  ~ObjectRegistry();

  JDWP::ObjectId Add(ObjPtr<mirror::Object> o)
      REQUIRES_SHARED(Locks::mutator_lock_)
      REQUIRES(!Locks::thread_list_lock_, !Locks::thread_suspend_count_lock_, !lock_);

  JDWP::RefTypeId AddRefType(ObjPtr<mirror::Class> c)
      REQUIRES_SHARED(Locks::mutator_lock_)
      REQUIRES(!Locks::thread_list_lock_, !Locks::thread_suspend_count_lock_, !lock_);

  template<class T>
  JDWP::ObjectId Add(Handle<T> obj_h)
      REQUIRES_SHARED(Locks::mutator_lock_)
      REQUIRES(!Locks::thread_list_lock_, !Locks::thread_suspend_count_lock_, !lock_);

  JDWP::RefTypeId AddRefType(Handle<mirror::Class> c_h)
      REQUIRES_SHARED(Locks::mutator_lock_)
      REQUIRES(!Locks::thread_list_lock_, !Locks::thread_suspend_count_lock_, !lock_);

  template<typename T> T Get(JDWP::ObjectId id, JDWP::JdwpError* error)
      REQUIRES_SHARED(Locks::mutator_lock_) REQUIRES(!lock_) {
    if (id == 0) {
      *error = JDWP::ERR_NONE;
      return nullptr;
    }
    return down_cast<T>(InternalGet(id, error));
  }

  void Clear() REQUIRES_SHARED(Locks::mutator_lock_) REQUIRES(!lock_);

  void DisableCollection(JDWP::ObjectId id)
      REQUIRES_SHARED(Locks::mutator_lock_) REQUIRES(!lock_);

  void EnableCollection(JDWP::ObjectId id)
      REQUIRES_SHARED(Locks::mutator_lock_) REQUIRES(!lock_);

  bool IsCollected(JDWP::ObjectId id)
      REQUIRES_SHARED(Locks::mutator_lock_) REQUIRES(!lock_);

  void DisposeObject(JDWP::ObjectId id, uint32_t reference_count)
      REQUIRES_SHARED(Locks::mutator_lock_) REQUIRES(!lock_);

  // This is needed to get the jobject instead of the Object*.
  // Avoid using this and use standard Get when possible.
  jobject GetJObject(JDWP::ObjectId id) REQUIRES_SHARED(Locks::mutator_lock_) REQUIRES(!lock_);

 private:
  template<class T>
  JDWP::ObjectId InternalAdd(Handle<T> obj_h)
      REQUIRES_SHARED(Locks::mutator_lock_)
      REQUIRES(!lock_, !Locks::thread_list_lock_, !Locks::thread_suspend_count_lock_);

  mirror::Object* InternalGet(JDWP::ObjectId id, JDWP::JdwpError* error)
      REQUIRES_SHARED(Locks::mutator_lock_) REQUIRES(!lock_);

  void Demote(ObjectRegistryEntry& entry)
      REQUIRES_SHARED(Locks::mutator_lock_) REQUIRES(lock_);

  void Promote(ObjectRegistryEntry& entry)
      REQUIRES_SHARED(Locks::mutator_lock_) REQUIRES(lock_);

  bool ContainsLocked(Thread* self,
                      ObjPtr<mirror::Object> o,
                      int32_t identity_hash_code,
                      ObjectRegistryEntry** out_entry)
      REQUIRES(lock_) REQUIRES_SHARED(Locks::mutator_lock_);

  Mutex lock_ DEFAULT_MUTEX_ACQUIRED_AFTER;
  std::multimap<int32_t, ObjectRegistryEntry*> object_to_entry_ GUARDED_BY(lock_);
  SafeMap<JDWP::ObjectId, ObjectRegistryEntry*> id_to_entry_ GUARDED_BY(lock_);

  size_t next_id_ GUARDED_BY(lock_);
};

} } } } // namespace facebook::museum::MUSEUM_VERSION::art

#endif  // ART_RUNTIME_JDWP_OBJECT_REGISTRY_H_
