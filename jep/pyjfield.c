/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 4 c-style: "K&R" -*- */
/* 
   jep - Java Embedded Python

   Copyright (C) 2004 Mike Johnson

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software Foundation,
   Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.  
   
   
   ---
   Fields really don't have to be represented as python objects,
   but it is nice to have garbage collection and to be able
   to cast to PyObject.
*/ 	

#if HAVE_CONFIG_H
# include <config.h>
#endif

#if HAVE_UNISTD_H
# include <sys/types.h>
# include <unistd.h>
#endif

// shut up the compiler
#ifdef _POSIX_C_SOURCE
#  undef _POSIX_C_SOURCE
#endif
#include <jni.h>

// shut up the compiler
#ifdef _POSIX_C_SOURCE
#  undef _POSIX_C_SOURCE
#endif
#include "Python.h"

#include "pyjfield.h"
#include "pyjobject.h"
#include "pyjclass.h"
#include "util.h"

staticforward PyTypeObject PyJfield_Type;
staticforward PyMethodDef  pyjfield_methods[];

static void pyjfield_dealloc(PyJfield_Object *self);

static jmethodID classGetName = 0;
static jmethodID classGetType = 0;
static jmethodID classGetMod  = 0;
static jmethodID modIsStatic  = 0;

PyJfield_Object* pyjfield_new(JNIEnv *env,
                              jobject rfield,
                              PyJobject_Object *pyjobject) {
    PyJfield_Object *pyf;
    jclass           rfieldClass = NULL;
    jstring          jstr        = NULL;
    const char      *fieldName   = NULL;
    
    pyf              = PyObject_NEW(PyJfield_Object, &PyJfield_Type);
    pyf->rfield      = (*env)->NewGlobalRef(env, rfield);
    pyf->pyjobject   = pyjobject;
    pyf->env         = env;
    pyf->pyFieldName = NULL;
    pyf->fieldTypeId = -1;
    pyf->isStatic    = -1;
    pyf->init        = 0;

    
    // ------------------------------ get field name
    
    rfieldClass = (*env)->GetObjectClass(env, rfield);
    if(process_java_exception(env) || !rfieldClass)
        goto EXIT_ERROR;
    
    if(classGetName == 0) {
        classGetName = (*env)->GetMethodID(env,
                                           rfieldClass,
                                           "getName",
                                           "()Ljava/lang/String;");
        if(process_java_exception(env) || !classGetName)
            goto EXIT_ERROR;
    }
    
    jstr = (jstring) (*env)->CallObjectMethod(env,
                                              rfield,
                                              classGetName);
    if(process_java_exception(env) || !jstr)
        goto EXIT_ERROR;
    
    fieldName        = (*env)->GetStringUTFChars(env, jstr, 0);
    pyf->pyFieldName = PyString_FromString(fieldName);
    
    (*env)->ReleaseStringUTFChars(env, jstr, fieldName);
    (*env)->DeleteLocalRef(env, jstr);
    
    
    return pyf;
    
EXIT_ERROR:
    if(pyf)
        pyjfield_dealloc(pyf);
    return NULL;
}


static int pyjfield_init(PyJfield_Object *self) {
    jfieldID         fieldId;
    jclass           rfieldClass = NULL;
    jobject          fieldType   = NULL;
    jclass           modClass    = NULL;
    jint             modifier    = -1;
    jboolean         isStatic    = JNI_FALSE;
    JNIEnv          *env;
    
    env = self->env;

    // use a local frame so we don't have to worry too much about local refs.
    // make sure if this method errors out, that this is poped off again
    (*env)->PushLocalFrame(env, 20);
    if(process_java_exception(env))
        return 0;
    
    rfieldClass = (*env)->GetObjectClass(env, self->rfield);
    if(process_java_exception(env) || !rfieldClass)
        goto EXIT_ERROR;
    
    
    // ------------------------------ get fieldid
    
    fieldId = (*env)->FromReflectedField(env,
                                         self->rfield);
    if(process_java_exception(env) || !fieldId)
        goto EXIT_ERROR;
    
    self->fieldId = fieldId;
    
    
    // ------------------------------ get return type
    
    if(classGetType == 0) {
        classGetType = (*env)->GetMethodID(env,
                                           rfieldClass,
                                           "getType",
                                           "()Ljava/lang/Class;");
        if(process_java_exception(env) || !classGetType)
            goto EXIT_ERROR;
    }
    
    fieldType = (*env)->CallObjectMethod(env,
                                         self->rfield,
                                         classGetType);
    if(process_java_exception(env) || !fieldType)
        goto EXIT_ERROR;
    
    {
        jclass rclazz;

        rclazz = (*env)->GetObjectClass(env, fieldType);
        if(process_java_exception(env) || !rclazz)
            goto EXIT_ERROR;

        self->fieldTypeId = get_jtype(env, fieldType, rclazz);

        if(process_java_exception(env))
            goto EXIT_ERROR;
    }
    
    // ------------------------------ get isStatic
    
    if(self->isStatic != 1) {
        // call getModifers()
        if(classGetMod == 0) {
            classGetMod = (*env)->GetMethodID(env,
                                              rfieldClass,
                                              "getModifiers",
                                              "()I");
            if(process_java_exception(env) || !classGetMod)
                goto EXIT_ERROR;
        }
        
        modifier = (*env)->CallIntMethod(env,
                                         self->rfield,
                                         classGetMod);
        if(process_java_exception(env))
            goto EXIT_ERROR;
    
        modClass = (*env)->FindClass(env, "java/lang/reflect/Modifier");
        if(process_java_exception(env) || !modClass)
            goto EXIT_ERROR;
        
        if(modIsStatic == 0) {
            modIsStatic = (*env)->GetStaticMethodID(env,
                                                    modClass,
                                                    "isStatic",
                                                    "(I)Z");
            if(process_java_exception(env) || !modIsStatic)
                goto EXIT_ERROR;
        }
        
        isStatic = (*env)->CallStaticBooleanMethod(env,
                                                   modClass,
                                                   modIsStatic,
                                                   modifier);
        if(process_java_exception(env))
            goto EXIT_ERROR;

        if(isStatic == JNI_TRUE)
            self->isStatic = 1;
        else
            self->isStatic = 0;
    }    
    
    (*env)->PopLocalFrame(env, NULL);
    self->init = 1;
    return 1;
    
EXIT_ERROR:
    (*env)->PopLocalFrame(env, NULL);
    
    if(!PyErr_Occurred())
        PyErr_SetString(PyExc_RuntimeError, "Unknown");
    
    return 0;
}


static void pyjfield_dealloc(PyJfield_Object *self) {
#if USE_DEALLOC
    JNIEnv *env  = self->env;
    if(env) {
        if(self->rfield)
            (*env)->DeleteGlobalRef(env, self->rfield);
    }
    
    if(self->pyFieldName)
        Py_DECREF(self->pyFieldName);

    PyObject_Del(self);
#endif
}


int pyjfield_check(PyObject *obj) {
    if(PyObject_TypeCheck(obj, &PyJfield_Type))
        return 1;
    return 0;
}


// get value from java object field.
// returns new reference.
PyObject* pyjfield_get(PyJfield_Object *self) {
    PyObject *result = NULL;
    JNIEnv   *env;
    
    if(!self) {
        PyErr_Format(PyExc_RuntimeError, "Invalid self object.");
        return NULL;
    }
    
    if(!self->init) {
        if(!pyjfield_init(self) || PyErr_Occurred())
            return NULL;
    }
    
    env = self->env;
    
    switch(self->fieldTypeId) {

    case JSTRING_ID: {
        jstring     jstr;
        const char *str;
        
        if(self->isStatic)
            jstr = (jstring) (*env)->GetStaticObjectField(
                env,
                self->pyjobject->clazz,
                self->fieldId);
        else
            jstr = (jstring) (*env)->GetObjectField(env,
                                                    self->pyjobject->object,
                                                    self->fieldId);
        
        if(process_java_exception(env))
            return NULL;
        
        if(jstr == NULL) {
            Py_INCREF(Py_None);
            return Py_None;
        }
        
        str    = (*env)->GetStringUTFChars(env, jstr, 0);
        result = PyString_FromString(str);
        
        (*env)->ReleaseStringUTFChars(env, jstr, str);
        (*env)->DeleteLocalRef(env, jstr);
        break;
    }
        
    case JOBJECT_ID: {
        jobject obj;
        
        if(self->isStatic)
            obj = (*env)->GetStaticObjectField(env,
                                               self->pyjobject->clazz,
                                               self->fieldId);
        else
            obj = (*env)->GetObjectField(env,
                                         self->pyjobject->object,
                                         self->fieldId);

        if(process_java_exception(env))
            return NULL;
        
        if(obj == NULL) {
            Py_INCREF(Py_None);
            return Py_None;
        }
        
        result = pyjobject_new(env, obj);
        break;
    }

    case JINT_ID: {
        jint ret;
        
        if(self->isStatic)
            ret = (*env)->GetStaticIntField(env,
                                            self->pyjobject->clazz,
                                            self->fieldId);
        else
            ret = (*env)->GetIntField(env,
                                      self->pyjobject->object,
                                      self->fieldId);

        if(process_java_exception(env))
            return NULL;
        
        result = Py_BuildValue("i", ret);
        break;
    }

    case JSHORT_ID: {
        jshort ret;
        
        if(self->isStatic)
            ret = (*env)->GetStaticShortField(env,
                                              self->pyjobject->clazz,
                                              self->fieldId);
        else
            ret = (*env)->GetShortField(env,
                                        self->pyjobject->object,
                                        self->fieldId);
        
        if(process_java_exception(env))
            return NULL;
        
        result = Py_BuildValue("i", (int) ret);
        break;
    }

    case JDOUBLE_ID: {
        jdouble ret;
        
        if(self->isStatic)
            ret = (*env)->GetStaticDoubleField(env,
                                               self->pyjobject->clazz,
                                               self->fieldId);
        else
            ret = (*env)->GetDoubleField(env,
                                         self->pyjobject->object,
                                         self->fieldId);
        
        if(process_java_exception(env))
            return NULL;
        
        result = PyFloat_FromDouble(ret);
        break;
    }

    case JFLOAT_ID: {
        jfloat ret;
        
        if(self->isStatic)
            ret = (*env)->GetStaticFloatField(env,
                                              self->pyjobject->clazz,
                                              self->fieldId);
        else
            ret = (*env)->GetFloatField(env,
                                        self->pyjobject->object,
                                        self->fieldId);
        
        if(process_java_exception(env))
            return NULL;
        
        result = PyFloat_FromDouble((double) ret);
        break;
    }

    case JLONG_ID: {
        jlong ret;
        
        if(self->isStatic)
            ret = (*env)->GetStaticLongField(env,
                                             self->pyjobject->clazz,
                                             self->fieldId);
        else
            ret = (*env)->GetLongField(env,
                                       self->pyjobject->object,
                                       self->fieldId);
        
        if(process_java_exception(env))
            return NULL;
        
        result = PyLong_FromLongLong((long long) ret);
        break;
    }


    case JBOOLEAN_ID: {
        jboolean ret;
        
        if(self->isStatic)
            ret = (*env)->GetStaticBooleanField(env,
                                                self->pyjobject->clazz,
                                                self->fieldId);
        else
            ret = (*env)->GetBooleanField(env,
                                          self->pyjobject->object,
                                          self->fieldId);
        
        if(process_java_exception(env))
            return NULL;
        
        result = Py_BuildValue("i", ret);
        break;
    }
        
    default:
        PyErr_Format(PyExc_RuntimeError, "Unknown field type.");
        Py_INCREF(Py_None);
        return Py_None;
    }
    
    // shouldn't happen
    if(result == NULL) {
        Py_INCREF(Py_None);
        return Py_None;
    }

    return result;
}


int pyjfield_set(PyJfield_Object *self, PyObject *value) {
    PyObject *result = NULL;
    JNIEnv   *env;
    jvalue    jarg;
    
    if(!self) {
        PyErr_Format(PyExc_RuntimeError, "Invalid self object.");
        return -1;
    }
    
    if(!self->init) {
        if(!pyjfield_init(self) || PyErr_Occurred())
            return -1;
    }
    
    env = self->env;
    
    switch(self->fieldTypeId) {

    case JSTRING_ID:
        if(!pyarg_matches_jtype(env, value, JSTRING_TYPE, self->fieldTypeId)) {
            PyErr_Format(PyExc_RuntimeError, "Expected string.");
            return -1;
        }
        
        jarg = convert_pyarg_jvalue(env,
                                    value,
                                    JSTRING_TYPE,
                                    self->fieldTypeId,
                                    1);
        if(PyErr_Occurred())
            return -1;
        
        if(self->isStatic)
            (*env)->SetStaticObjectField(env,
                                         self->pyjobject->clazz,
                                         self->fieldId,
                                         jarg.l);
        else
            (*env)->SetObjectField(env,
                                   self->pyjobject->object,
                                   self->fieldId,
                                   jarg.l);
        
        (*env)->DeleteLocalRef(env, jarg.l);
        if(process_java_exception(env))
            return -1;
        
        return 0; // success
    
        
    case JOBJECT_ID:
        if(!pyarg_matches_jtype(env, value, JOBJECT_TYPE, self->fieldTypeId)) {
            PyErr_Format(PyExc_RuntimeError, "Expected object.");
            return -1;
        }
        
        jarg = convert_pyarg_jvalue(env,
                                    value,
                                    JOBJECT_TYPE,
                                    self->fieldTypeId,
                                    1);
        if(PyErr_Occurred())
            return -1;
        
        if(self->isStatic)
            (*env)->SetStaticObjectField(env,
                                         self->pyjobject->clazz,
                                         self->fieldId,
                                         jarg.l);
        else
            (*env)->SetObjectField(env,
                                   self->pyjobject->object,
                                   self->fieldId,
                                   jarg.l);
        
        (*env)->DeleteLocalRef(env, jarg.l);
        if(process_java_exception(env))
            return -1;
        
        return 0; // success

        
    case JINT_ID:
        if(!pyarg_matches_jtype(env, value, JINT_TYPE, self->fieldTypeId)) {
            PyErr_Format(PyExc_RuntimeError, "Expected int.");
            return -1;
        }
        
        jarg = convert_pyarg_jvalue(env,
                                    value,
                                    JINT_TYPE,
                                    self->fieldTypeId,
                                    1);
        if(PyErr_Occurred())
            return -1;
        
        if(self->isStatic)
            (*env)->SetStaticIntField(env,
                                      self->pyjobject->clazz,
                                      self->fieldId,
                                      jarg.i);
        else
            (*env)->SetIntField(env,
                                self->pyjobject->object,
                                self->fieldId,
                                jarg.i);

        if(process_java_exception(env))
            return -1;
        
        return 0; // success

        
    case JSHORT_ID:
        if(!pyarg_matches_jtype(env, value, JSHORT_TYPE, self->fieldTypeId)) {
            PyErr_Format(PyExc_RuntimeError, "Expected int.");
            return -1;
        }
        
        jarg = convert_pyarg_jvalue(env,
                                    value,
                                    JSHORT_TYPE,
                                    self->fieldTypeId,
                                    1);
        if(PyErr_Occurred())
            return -1;
        
        if(self->isStatic)
            (*env)->SetStaticShortField(env,
                                        self->pyjobject->clazz,
                                        self->fieldId,
                                        jarg.s);
        else
            (*env)->SetShortField(env,
                                  self->pyjobject->object,
                                  self->fieldId,
                                  jarg.s);

        if(process_java_exception(env))
            return -1;
        
        return 0; // success


    case JDOUBLE_ID:
        if(!pyarg_matches_jtype(env, value, JDOUBLE_TYPE, self->fieldTypeId)) {
            PyErr_Format(PyExc_RuntimeError, "Expected float (jdouble).");
            return -1;
        }
        
        jarg = convert_pyarg_jvalue(env,
                                    value,
                                    JDOUBLE_TYPE,
                                    self->fieldTypeId,
                                    1);
        if(PyErr_Occurred())
            return -1;
        
        if(self->isStatic)
            (*env)->SetStaticDoubleField(env,
                                         self->pyjobject->clazz,
                                         self->fieldId,
                                         jarg.d);
        else
            (*env)->SetDoubleField(env,
                                   self->pyjobject->object,
                                   self->fieldId,
                                   jarg.d);
        
        if(process_java_exception(env))
            return -1;
        
        return 0; // success


    case JFLOAT_ID:
        if(!pyarg_matches_jtype(env, value, JFLOAT_TYPE, self->fieldTypeId)) {
            PyErr_Format(PyExc_RuntimeError, "Expected float (jfloat).");
            return -1;
        }
        
        jarg = convert_pyarg_jvalue(env,
                                    value,
                                    JFLOAT_TYPE,
                                    self->fieldTypeId,
                                    1);
        if(PyErr_Occurred())
            return -1;
        
        if(self->isStatic)
            (*env)->SetStaticFloatField(env,
                                        self->pyjobject->clazz,
                                        self->fieldId,
                                        jarg.f);
        else
            (*env)->SetFloatField(env,
                                  self->pyjobject->object,
                                  self->fieldId,
                                  jarg.f);
        
        if(process_java_exception(env))
            return -1;
        
        return 0; // success


    case JLONG_ID:
        if(!pyarg_matches_jtype(env, value, JLONG_TYPE, self->fieldTypeId)) {
            PyErr_Format(PyExc_RuntimeError, "Expected long.");
            return -1;
        }
        
        jarg = convert_pyarg_jvalue(env,
                                    value,
                                    JLONG_TYPE,
                                    self->fieldTypeId,
                                    1);
        if(PyErr_Occurred())
            return -1;
        
        if(self->isStatic)
            (*env)->SetStaticLongField(env,
                                       self->pyjobject->clazz,
                                       self->fieldId,
                                       jarg.j);
        else
            (*env)->SetLongField(env,
                                 self->pyjobject->object,
                                 self->fieldId,
                                 jarg.j);
        
        if(process_java_exception(env))
            return -1;
        
        return 0; // success


    case JBOOLEAN_ID:
        if(!pyarg_matches_jtype(env, value, JBOOLEAN_TYPE, self->fieldTypeId)) {
            PyErr_Format(PyExc_RuntimeError, "Expected boolean.");
            return -1;
        }
        
        jarg = convert_pyarg_jvalue(env,
                                    value,
                                    JBOOLEAN_TYPE,
                                    self->fieldTypeId,
                                    1);
        if(PyErr_Occurred())
            return -1;
        
        if(self->isStatic)
            (*env)->SetStaticBooleanField(env,
                                          self->pyjobject->clazz,
                                          self->fieldId,
                                          jarg.z);
        else
            (*env)->SetBooleanField(env,
                                    self->pyjobject->object,
                                    self->fieldId,
                                    jarg.z);

        if(process_java_exception(env))
            return -1;
        
        return 0; // success
    }
    

    PyErr_Format(PyExc_RuntimeError, "Unknown field type.");
    return -1;
}


static PyMethodDef pyjfield_methods[] = {
    {NULL, NULL, 0, NULL}
};


static PyTypeObject PyJfield_Type = {
    PyObject_HEAD_INIT(&PyType_Type)
    0,
    "PyJfield",
    sizeof(PyJfield_Object),
    0,
    (destructor) pyjfield_dealloc,            /* tp_dealloc */
    0,                                        /* tp_print */
    0,                                        /* tp_getattr */
    0,                                        /* tp_setattr */
    0,                                        /* tp_compare */
    0,                                        /* tp_repr */
    0,                                        /* tp_as_number */
    0,                                        /* tp_as_sequence */
    0,                                        /* tp_as_mapping */
    0,                                        /* tp_hash  */
    0,                                        /* tp_call */
    0,                                        /* tp_str */
    0,                                        /* tp_getattro */
    0,                                        /* tp_setattro */
    0,                                        /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT,                       /* tp_flags */
    "jfield",                                 /* tp_doc */
    0,                                        /* tp_traverse */
    0,                                        /* tp_clear */
    0,                                        /* tp_richcompare */
    0,                                        /* tp_weaklistoffset */
    0,                                        /* tp_iter */
    0,                                        /* tp_iternext */
    pyjfield_methods,                         /* tp_methods */
    0,                                        /* tp_members */
    0,                                        /* tp_getset */
    0,                                        /* tp_base */
    0,                                        /* tp_dict */
    0,                                        /* tp_descr_get */
    0,                                        /* tp_descr_set */
    0,                                        /* tp_dictoffset */
    (initproc) PyType_GenericNew,             /* tp_init */
    0,                                        /* tp_alloc */
    NULL,                                     /* tp_new */
};
