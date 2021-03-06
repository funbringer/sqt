select 
    'role' node_type,
    rolname ui_name,
    oid id,
    false is_parent,
    case 
        when not rolcanlogin then 'users.png'
        when rolsuper then 'ghost.png' 
        else 'user-medium.png' 
    end icon,
    (not rolcanlogin)::int::text sort1
from pg_catalog.pg_roles
