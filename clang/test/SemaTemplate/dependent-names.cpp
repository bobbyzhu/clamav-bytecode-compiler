// RUN: %clang_cc1 -fsyntax-only -verify %s 

typedef double A;
template<typename T> class B {
  typedef int A;
};

template<typename T> struct X : B<T> {
  static A a;
};

int a0[sizeof(X<int>::a) == sizeof(double) ? 1 : -1];

// PR4365.
template<class T> class Q;
template<class T> class R : Q<T> {T current;};


namespace test0 {
  template <class T> class Base {
    void instance_foo();
    static void static_foo();
    class Inner {
      void instance_foo();
      static void static_foo();
    };
  };

  template <class T> class Derived1 : Base<T> {
    void test0() {
      Base<T>::static_foo();
      Base<T>::instance_foo();
    }

    void test1() {
      Base<T>::Inner::static_foo();
      Base<T>::Inner::instance_foo(); // expected-error {{call to non-static member function without an object argument}}
    }

    static void test2() {
      Base<T>::static_foo();
      Base<T>::instance_foo(); // expected-error {{call to non-static member function without an object argument}}
    }

    static void test3() {
      Base<T>::Inner::static_foo();
      Base<T>::Inner::instance_foo(); // expected-error {{call to non-static member function without an object argument}}
    }
  };

  template <class T> class Derived2 : Base<T>::Inner {
    void test0() {
      Base<T>::static_foo();
      Base<T>::instance_foo(); // expected-error {{call to non-static member function without an object argument}}
    }

    void test1() {
      Base<T>::Inner::static_foo();
      Base<T>::Inner::instance_foo();
    }

    static void test2() {
      Base<T>::static_foo();
      Base<T>::instance_foo(); // expected-error {{call to non-static member function without an object argument}}
    }

    static void test3() {
      Base<T>::Inner::static_foo();
      Base<T>::Inner::instance_foo(); // expected-error {{call to non-static member function without an object argument}}
    }
  };

  void test0() {
    Derived1<int> d1;
    d1.test0();
    d1.test1(); // expected-note {{in instantiation of member function}}
    d1.test2(); // expected-note {{in instantiation of member function}}
    d1.test3(); // expected-note {{in instantiation of member function}}

    Derived2<int> d2;
    d2.test0(); // expected-note {{in instantiation of member function}}
    d2.test1();
    d2.test2(); // expected-note {{in instantiation of member function}}
    d2.test3(); // expected-note {{in instantiation of member function}}
  }
}

namespace test1 {
  template <class T> struct Base {
    void foo(T); // expected-note {{must qualify identifier to find this declaration in dependent base class}}
  };

  template <class T> struct Derived : Base<T> {
    void doFoo(T v) {
      foo(v); // expected-error {{use of undeclared identifier}}
    }
  };

  template struct Derived<int>; // expected-note {{requested here}}
}
