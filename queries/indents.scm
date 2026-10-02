(pair
  ":" @start
  value: [(object) (array_body) (keyed_body)]) @indent
(array
  ":" @start
  value: (array_body)) @indent
(keyed_object
  ":" @start
  value: (keyed_body)) @indent
(object_row
  (object)) @indent
